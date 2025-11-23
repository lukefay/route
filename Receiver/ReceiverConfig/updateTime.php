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

$pyth="C:/Users/1000049321/AppData/Local/Programs/Python/Python38-32/python.exe";
$Delay = -4.0;
$MPD = $_REQUEST['mpd'];

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

$AST_SEC = new DateTime( 'now',  new DateTimeZone( 'UTC' ) );	/* initializer for availability start time */
$AST_SEC->setTimestamp($date_array[1]);    //Better use a single time than now above
$AST_SEC_W3C = $AST_SEC->format(DATE_W3C);

# Read PTP time from the PHY
$PTP = floatval(file_get_contents("PTP_TIME.dat"));
//if (!$PTP) die("Failed loading PTP time");
if (!$PTP) {
	$PTP = $AST_SEC_W3C;
}

# Read UTC offset from PTP time with LLS table 0x03 (SystemTime)
$ST = simplexml_load_file("SystemTime.xml");
if (!$ST) die("Failed loading XML file");

$ST_UTC = intval($ST['currentUtcOffset']);

usleep(1000);


$AST_BCST = $PTP - $ST_UTC - $Delay;

preg_match('/\.\d*/',$date_array[0],$dateFracPart);
$extension_pos = strrpos($AST_SEC_W3C, '+'); // find position of the last + in W3C date to slip frac seconds
$AST_W3C = substr($AST_SEC_W3C, 0, $extension_pos) . $dateFracPart[0] . "Z" ; //substr($AST_SEC_W3C, $extension_pos);

$dom_sxe = dom_import_simplexml($MPD);
if (!$dom_sxe) {
	echo 'Error while converting XML';
	exit;
}

$dom = new DOMDocument('1.0');
$dom_sxe = $dom->importNode($dom_sxe, true);
$dom_sxe = $dom->appendChild($dom_sxe);

$periods = parseMPD($dom->documentElement);


$cumulativeUpdatedDuration = 0;    //Cumulation of period duration on updated MPD
$tuneInPeriodStart = 0;

$MPDNode = &$periods[0]['node']->parentNode;

$MPD_AST = $MPDNode->getAttribute("availabilityStartTime");
$originalAST = new DateTime($MPD_AST);   
$deltaTimeASTTuneIn = $AST_SEC->getTimestamp() - $AST_BCST;  //Time elapsed between the original PHY time and Tune-in time

$ASTNew = date("Y-m-d H:i:s", ($originalAST->getTimestamp() + $deltaTimeASTTuneIn)) . "Z";
#$MPDNode->setAttribute("availabilityStartTime",date("Y-m-d H:i:s", ($originalAST->getTimestamp() + $deltaTimeASTTuneIn)) . "Z");    //Set AST to tune-in time
$MPDNode->setAttribute("availabilityStartTime",str_replace(" ", "T", $ASTNew));    //Set AST to tune-in time

$dom->save($DASHContent . "\\" . $MPD);

usleep(1000);

chdir('../../');

function &parseMPD($docElement) {
	foreach ($docElement->childNodes as $node) {
		//echo $node->nodeName; // body
		if($node->nodeName === 'Location') $locationNode = $node;
		if($node->nodeName === 'BaseURL') $baseURLNode = $node;    
		if($node->nodeName === 'Period') {
			$periods[]['node'] = $node;
			
			$currentPeriod = &$periods[count($periods) - 1];
			foreach ($currentPeriod['node']->childNodes as $node) {
				if($node->nodeName === 'AdaptationSet') {
					$currentPeriod['adaptationSet'][]['node'] = $node;
					
					$currentAdaptationSet = &$currentPeriod['adaptationSet'][count($currentPeriod['adaptationSet']) - 1];                    
					foreach ($currentAdaptationSet['node']->childNodes as $node) {
						if($node->nodeName === 'Representation') {
							$currentAdaptationSet['representation'][]['node'] = $node;
							
							$currentRepresentation = &$currentAdaptationSet['representation'][count($currentAdaptationSet['representation']) - 1];
							
							foreach ($currentRepresentation['node']->childNodes as $node) {
								if($node->nodeName === 'SegmentTemplate') $currentRepresentation['segmentTemplate']['node'] = $node;
							}
						}
					}
				}
			}
		}
	}
	
	return $periods;
}



?>
