<?php

/* 
Main script for Video playback
 */

if (substr(php_uname(), 0, 7) == "Windows") {
  $FFPLAY = "C:/Users/luke/Documents/Work/ffmpeg-6.1.1-full_build/bin/ffplay.exe";
} else {
  $FFPLAY = "/usr/bin/ffplay";
}
$mpd = $_REQUEST['mpdURL'];

// Start FFMPEG Player
//$play = "$FFPLAY -report -allowed_extensions ALL -alwaysontop -x 640 -y 480 -left 0 -top 0 -i $mpd";
//$play = "$FFPLAY -fs -report -allowed_extensions ALL -alwaysontop -sync video -noborder -i $mpd";
//$play = "C:/Users/luke/Documents/Work/ffmpeg-6.1.1-full_build/bin/ffplay.exe -report -allowed_extensions ALL -alwaysontop -i http://127.0.0.1:8080/Route_Receiver/Receiver/DASH_Content3/MultiRate_Dynamic_Patched.mpd";
//$play = "C:/Users/luke/Documents/Work/ffmpeg-6.1.1-full_build/bin/ffplay.exe -report -allowed_extensions ALL -alwaysontop -i $mpd";
$play = $FFPLAY . " -report -allowed_extensions ALL -alwaysontop -noborder -i " . $mpd;
if (substr(php_uname(), 0, 7) == "Windows") {
  //pclose(popen("start /B ". $play, "r"));
  //pclose(popen("cmd /c ". $play, "r"));
  system("start /B " . $play);
  //passthru("start " . $play);
  //shell_exec('SCHTASKS /F /Create /TN _notepad /TR "notepad.exe" /SC DAILY /RU INTERACTIVE');
  //shell_exec('SCHTASKS /RUN /TN "_notepad"');
  //shell_exec('SCHTASKS /DELETE /TN "_notepad" /F');
  //shell_exec('SCHTASKS /F /Create /TN _ffplay /TR "$play" ');
  //shell_exec('SCHTASKS /RUN /TN "_ffplay"');
  //shell_exec('SCHTASKS /DELETE /TN "_ffplay" /F');
} else {
  exec($play . " > /dev/null &");
}

?>