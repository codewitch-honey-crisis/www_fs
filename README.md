# Web file browser

This firmware allows you to browse SPIFFS or an attached SD card and view, download, delete and upload contents.

It's written in the ESP-IDF, and the project is made for platformIO. Several devices are supported, and it's trivial to add more.

See the [ClASP documentation](https://github.com/codewitch-honey-crisis/clasp) for details on the contents of www

Note that you can use ports.ini to set your monitor and upload ports. It's not in the platformio.ini file so that you can change it without triggering a rebuild.