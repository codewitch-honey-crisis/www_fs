# Web file browser

This little bit of firmware allows you to browse SPIFFS or an attached SD card and view, download, delete and upload contents.

If your device has a neopixel it can change color based on activity (blue = download, red = upload)

It's written with the ESP-IDF, and the project is made for platformIO. Several devices are supported, and it's trivial to add more. The most conmplicated thing involves adding long file name support under the FAT FS support in menuconfig, and then setting the HTTP server header size to 1024 instead of 512

See the [ClASP documentation](https://github.com/codewitch-honey-crisis/clasp) for details on the contents of www

Note that you can use ports.ini to set your monitor and upload ports. It's not in the platformio.ini file so that you can change it without triggering a rebuild.

Using:

Choose your device, and/or add one in platformio.ini

Under ./data add a wifi.txt file with your SSID on one line, and password on the next line.

Upload Filesystem Image under Project Tasks.

Finally, upload and monitor.