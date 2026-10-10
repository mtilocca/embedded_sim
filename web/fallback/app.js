var o=document.getElementById('o'),w=new WebSocket('ws://'+location.host+'/ws');
w.onmessage=function(e){o.textContent=(e.data+'\n'+o.textContent).slice(0,4000)};
w.onclose=function(){o.textContent='disconnected\n'+o.textContent};
