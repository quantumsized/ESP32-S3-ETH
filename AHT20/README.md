This is one I personally needed.
What it is set to do is talk over ethernet to my MQTT broker (mosquitto) and using the PoE hat in order to not need a battery connected to the ESP32.
As I have well distributed ethernet infrastructure, it made good sense to use it.
The code might not be well commented but there are a number of them to help understanding of what is what.
Also, I disabled WiFi as I would not need it for this project.
The current Arduino IDE version at the time of this first commit is 2.3.6 and I'm runing on Debian 12
