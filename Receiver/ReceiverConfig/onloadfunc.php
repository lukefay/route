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
exec("taskkill /IM python3 /F");

if (substr(php_uname(), 0, 7) == "Windows") {
  $pyth="C:/Users/1000049321/AppData/Local/Programs/Python/Python38-32/python.exe";
} else {
  $pyth="/usr/bin/python";
}

chdir('../SLT_signalling');

if (substr(php_uname(), 0, 7) == "Windows") {
  //pclose(popen("start \"bla\" \"" . $pyth . "\" " . escapeshellarg("receiver.py"), "r"));
  pclose(popen("start /B ". $pyth . " receiver.py", "r"));
  //$WshShell = new COM("WScript.Shell");
  //$oExec = $WshShell->Run("$pyth receiver.py", 0, false);
} else {
  //exec("$pyth receiver.py");
  //exec("sudo python ../SLT_signalling/receiver.py");
  exec("sudo $pyth receiver.py" . " > /dev/null &");
}

if (substr(php_uname(), 0, 7) == "Windows") {
  //pclose(popen("start \"bla\" \"" . $pyth . "\" " . escapeshellarg("time.py"), "r"));
  pclose(popen("start /B ". $pyth . " time.py", "r"));
  //$WshShell = new COM("WScript.Shell");
  //$oExec = $WshShell->Run("$pyth time.py", 0, false);
} else {
  //exec("$pyth time.py");
  //exec("sudo python ../SLT_signalling/time.py");
  exec("sudo $pyth time.py" . " > /dev/null &");
}


// Do NOT change System Time
//if (substr(php_uname(), 0, 7) == "Windows") {
//  pclose(popen("start \"tyme\" \"" . $pyth . "\" " . escapeshellarg("readSystemTime.py"), "r"));
//} else {
//  exec("$pyth readSystemTime.py");
//}

chdir('../ReceiverConfig');

// Have to modify the file to get the IP from the SLT.xml file itself.
//$contents = file_get_contents("../../bin/SDP1.sdp") ;
// Until we look at the STL, use local IP address
if (substr(php_uname(), 0, 7) == "Windows") {
  $contents=shell_exec("ipconfig");
} else {
  $contents=shell_exec("ifconfig");
}

$pos = 0;
$index = 0;
$ip = "127.0.0.1";

while(true) {
	if (substr(php_uname(), 0, 7) == "Windows") {
	  $findme="  IPv4 Address";
	} else {
	  $findme="  inet ";	
	}
	//$pos = strpos($contents, $findme);
	$pos = strpos($contents, $findme, $pos + strlen($findme));
	if($pos === FALSE) break;
	if (substr(php_uname(), 0, 7) == "Windows") {
	  $start=$pos+38;
	  $end=strpos($contents,"\n",$start);
	} else {
	  $start=$pos+7;
	  $end=strpos($contents,"  ",$start);	
	}
	$thisIP=substr($contents,$start,$end-$start);
	if($thisIP !== "127.0.0.1") {
		$ip=$thisIP;
	}
}
//echo $ip;
echo json_encode($ip);
?>
