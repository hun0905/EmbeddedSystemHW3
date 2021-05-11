HW3
===
1.首先用 $git clone https://github.com/hun0905/HW3.git    將所需的檔案抓下來<br> 
2.調整mbed_app.json的WiFi SSID,PASSWORD 以及 wifi_mqtt/mqtt_client.py的host<br> 
3.然後 $ cd HW3/src/model_deploy<br> 
4.輸入$ sudo mbed compile --source . --source ~/ee2405/mbed-os/ -m B_L4S5I_IOT01A -t GCC_ARM -f 進行編譯<br> 
5.在另外一個terminal $ cd HW3/src/model_deploy  後用 $sudo python3 wifi_mqtt/mqtt_client.py 執行   python 檔<br> 
6.輸入$ sudo screen /dev/<devicename> (<devicename> ex:ttyACM0)<br> 
7.在成功連線後 輸入/gesture_UI/run 會進入角度選擇的模式<br> 
8.用mbed的手勢來調整角度(30,40,50,60)， 調到30就用板子畫3、40就４、50就5、60就6。<br> 
9.出現要的角度後按 USER BUTTON來確認， 之後ulcd會顯示出選定的角度，這時候mqtt_client.py 接收角度並傳送指令使角度選擇模式停止，進入tilt 測量模式。<br> 
10.在tilt測量模式中ulcd 會顯示目前測到角度，當角度超過了之前所選定的角度後，會顯示訊息通知角度大於所設定的值並停止測量。<br> 
11.要離開tilt測量模式按下  USER BUTTON後mqtt_client.py 就會收到訊息，使其回到原來的模式。(不同模式可用led 燈來區別)。<br> 
  
  
