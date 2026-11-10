// Embedded-AI simulation: bearing condition monitoring on the MCU.
//
// A scripted "machine" cycles through health conditions (normal -> inner-race
// fault -> normal -> ball fault -> normal -> outer-race fault). Each period the
// sim "acquires" one 512-sample vibration window (replayed from real CWRU
// accelerometer recordings of the held-out 3 hp load, see python/), normalises
// it exactly as in training, and classifies it with an int8 CNN running on
// TensorFlow Lite Micro (CMSIS-NN kernels on the Cortex-M33).
#include "core/simulation.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <cmath>
#include <cstdint>

#include "net/telemetry.h"
#include "gen/model_data.h"
#include "gen/window_bank.h"

#include <tensorflow/lite/micro/micro_interpreter.h>
#include <tensorflow/lite/micro/micro_mutable_op_resolver.h>
#include <tensorflow/lite/schema/schema_generated.h>

LOG_MODULE_REGISTER(sim_bearing_ai, LOG_LEVEL_INF);

namespace sim {
namespace {

using namespace bearing_model;

constexpr size_t kArenaBytes = 24 * 1024;
alignas(16) uint8_t g_arena[kArenaBytes];

// Condition schedule: class index per phase.
constexpr int kSchedule[] = {0, 1, 0, 2, 0, 3};
constexpr int kPhases = sizeof(kSchedule) / sizeof(kSchedule[0]);

class BearingAi : public Simulation {
public:
	const char *name() const override { return "bearing_ai"; }
	uint32_t period_ms() const override { return 100; }
	int priority() const override { return 8; }

	int init() override
	{
		const tflite::Model *m = tflite::GetModel(kModel);
		if (m->version() != TFLITE_SCHEMA_VERSION) {
			LOG_ERR("model schema %u != %d", (unsigned)m->version(), TFLITE_SCHEMA_VERSION);
			return -EINVAL;
		}
		static tflite::MicroMutableOpResolver<3> resolver;
		resolver.AddConv2D();
		resolver.AddMean();
		resolver.AddFullyConnected();
		static tflite::MicroInterpreter interp(m, resolver, g_arena, kArenaBytes);
		interp_ = &interp;
		if (interp.AllocateTensors() != kTfLiteOk) {
			LOG_ERR("AllocateTensors failed (arena %u B)", (unsigned)kArenaBytes);
			return -ENOMEM;
		}
		in_ = interp.input(0);
		out_ = interp.output(0);
		if (in_->type != kTfLiteInt8 || out_->type != kTfLiteInt8 ||
		    in_->bytes != (size_t)kWindow) {
			LOG_ERR("unexpected tensor types/sizes");
			return -EINVAL;
		}
		LOG_INF("model %u B, arena used %u of %u B, input scale %.5f zp %d",
			(unsigned)kModelLen, (unsigned)interp.arena_used_bytes(), (unsigned)kArenaBytes,
			(double)in_->params.scale, (int)in_->params.zero_point);
		ready_ = true;
		return 0;
	}

	void step(const rt::Tick &t) override
	{
		if (!ready_) {
			return;
		}
		const uint32_t phase_len = CONFIG_SIM_BEARING_AI_DWELL_S * 1000 / period_ms();
		const int phase = (t.seq / phase_len) % kPhases;
		const int truth = kSchedule[phase];
		const int16_t *raw = kBank[truth * kBankPerClass + (t.seq % kBankPerClass)];

		// --- acquisition + preprocessing (same maths as python/bearing/data.py)
		float mean = 0;
		for (int i = 0; i < kWindow; i++) {
			mean += raw[i];
		}
		mean /= kWindow;
		float var = 0;
		for (int i = 0; i < kWindow; i++) {
			const float d = raw[i] - mean;
			var += d * d;
		}
		const float sd = sqrtf(var / kWindow);
		const float inv = 1.0f / (sd + 1e-6f * kLsbPerG);
		int8_t *in = in_->data.int8;
		for (int i = 0; i < kWindow; i++) {
			const float z = (raw[i] - mean) * inv;
			long q = lroundf(z / kInputScale) + kInputZeroPoint;
			in[i] = (int8_t)(q < -128 ? -128 : q > 127 ? 127 : q);
		}

		// --- inference, timed with the cycle counter
		const uint32_t c0 = k_cycle_get_32();
		const TfLiteStatus st = interp_->Invoke();
		const uint32_t cyc = k_cycle_get_32() - c0;
		if (st != kTfLiteOk) {
			LOG_ERR("Invoke failed");
			return;
		}
		const uint32_t infer_us = (uint32_t)(((uint64_t)cyc * 1000000ULL) / sys_clock_hw_cycles_per_sec());

		float p[kClasses];
		float mx = -1e30f;
		for (int i = 0; i < kClasses; i++) {
			p[i] = (out_->data.int8[i] - kLogitZeroPoint) * kLogitScale;
			mx = p[i] > mx ? p[i] : mx;
		}
		float sum = 0;
		for (int i = 0; i < kClasses; i++) {
			p[i] = expf(p[i] - mx);
			sum += p[i];
		}
		int pred = 0;
		for (int i = 0; i < kClasses; i++) {
			p[i] /= sum;
			pred = p[i] > p[pred] ? i : pred;
		}

		if (t.seq % 5 != 0) { // inference at 10 Hz, telemetry at 2 Hz
			return;
		}
		const float rms_g = sd / kLsbPerG;
		net::telemetry::publish("bearing_ai", "p_normal", {p[0], "prob"});
		net::telemetry::publish("bearing_ai", "p_inner", {p[1], "prob"});
		net::telemetry::publish("bearing_ai", "p_ball", {p[2], "prob"});
		net::telemetry::publish("bearing_ai", "p_outer", {p[3], "prob"});
		net::telemetry::publish("bearing_ai", "anomaly_score", {1.0f - p[0], "prob"});
		net::telemetry::publish("bearing_ai", "predicted_class", {(float)pred, "class"});
		net::telemetry::publish("bearing_ai", "true_class", {(float)truth, "class"});
		net::telemetry::publish("bearing_ai", "infer_us", {(float)infer_us, "us"});
		net::telemetry::publish("bearing_ai", "vib_rms", {rms_g, "g"});
	}

private:
	tflite::MicroInterpreter *interp_ = nullptr;
	TfLiteTensor *in_ = nullptr;
	TfLiteTensor *out_ = nullptr;
	bool ready_ = false;
};

} // namespace

Simulation &bearing_ai()
{
	static BearingAi instance;
	return instance;
}

} // namespace sim
