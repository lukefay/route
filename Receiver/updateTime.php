<?php

/* 
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

header('Content-Type: text/event-stream');
// recommended to prevent caching of event data.
header('Cache-Control: no-cache'); 

// Kill any previous LLS
exec("taskkill /IM python.exe /F");

$pyth="C:/Users/luke/AppData/Local/Programs/Python/Python38-32/python.exe";

chdir('./Receiver/SLT_signalling');

if (substr(php_uname(), 0, 7) == "Windows") {
  //pclose(popen("start \"bla\" \"" . $pyth . "\" " . escapeshellarg("time.py"), "r"));
  pclose(popen("start /B ". $pyth . " time.py", "r"));
  //$WshShell = new COM("WScript.Shell");
  //$oExec = $WshShell->Run("$pyth time.py", 0, false);
} else {
  //exec("$pyth time.py");
  //exec("sudo python ../SLT_signalling/time.py");
  exec("$pyth time.py" . " > /dev/null &");
}

chdir('../../');

?>
