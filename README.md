# esp32-cam-examples


1. Basic
- Wi-Fi
- Camera

2. White balance and Exposure
- Auto white balance
- Auto Exposure

3. Html Page
- Very basic html page
This version has issues as its only using 1 core for all the code execution. meaning when the streams loaded no more other requests can be made

4. Task to core pinning
- 1st core: Html webserver for html
- 2nd core: Webserver for image stream on port `81`