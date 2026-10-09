# source scripts/env.sh
source "$HOME/zephyrproject/env.sh"
export SIM_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
