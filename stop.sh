kill $(ps aux | grep '[p]ython3 server.py' | awk '{print $1}')