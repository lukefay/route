<?php

/* 
Main script for starting ROUTE reception
 */
header('Content-Type: text/event-stream');
# recommended to prevent caching of event data.
header('Cache-Control: no-cache'); 
 
$micro_date = microtime();
$date_array = explode(" ",$micro_date);
$date = date("Y-m-d H:i:s",$date_array[1]);
unlink ('../bin/timelog.txt');
file_put_contents ( "timelog.txt" , "Start:" . $date . $date_array[0] . " \r\n" , FILE_APPEND );

ini_set('memory_limit','-1');//remove memory limit

/* 
Main script for starting flute reception and MPD re-writing
 */
$AdSource = intval(file_get_contents("RcvConfig.txt"));
chdir('../bin/');
$currDir=dirname(__FILE__);

$channel = $_REQUEST['channel'];
$responseToSend = array();
$responseToSend[0] = $channel;
#echo "Started channel ". $channel;

#Define Paths
$DASHContentBase="DASH_Content";
$DASHContentDir=$DASHContentBase . (string)$channel;
#$DASHContent=$currDir . "/" . $DASHContentDir;
$DASHContent=$currDir . "\\" . $DASHContentDir;

# Define Variables
#$OriginalMPD= "MultiRate_Dynamic.mpd";
$OriginalMPD= "ManifestUpdate_Dynamic.mpd";
$AdMPDName="Ad2/Ad2_MultiRate.mpd";

$Delay=4.5;	#How much would the AST of the patched MPD be lagging the current system time, i.e. how far in future is the AST (in seconds)?
$PatchedMPD="MultiRate_Dynamic_Patched.mpd";
$FLUTEReceiver="C:/Users/luke/Documents/Work/Route_Receiver/Debug";
#$FLUTEReceiver="C:/Users/luke/Documents/Work/Route_Receiver/Release";
#HTMLLocalStorage="/home/nomor/.config/google-chrome-unstable/Default/Local Storage/"


$Log="Rcv_Log_MPD" . (string)$channel . ".txt";		#Log containing delays corresponding to FLUTE receiver
$encodingSymbolsPerPacket=0;	#For Receiver, Only a value of zero makes a difference. Otherwise, it is ignored 
							#This means that more than one encoding symbol is included packet. This could be varying

#Clear HTML5 Local Storage
#if [ -e "$HTMLLocalStorage"*${Client:7}*localstorage-journal -o -e "$HTMLLocalStorage"*${Client:7}*localstorage ]; then
#  echo "Delete Old HTML Local Storage"
#  rm "$HTMLLocalStorage"*${Client:7}*
#fi

#Initialize DASHContent Folder
exec("mkdir $DASHContent");
#array_map('unlink', glob("$DASHContent/*"));
array_map('unlink', glob("$DASHContent\\*"));

#In case previous instances are running, stop them
#exec("sudo killall flute");
exec("taskkill /F /IM flute.exe /T");
exec("taskkill /F /IM a3route.exe /T");
usleep(5000);

#Start ROUTE Protocol operation by reading LLS @ 224.0.23.60:4937
chdir('../Receiver/SLT_signalling');
$pyth="C:/Users/luke/AppData/Local/Programs/Python/Python38-32/python.exe";
#$result = json_decode(exec('sudo python readFromSLT.py ' . $channel), true);
$result = json_decode(shell_exec("$pyth readFromSLT.py " . $channel), true);
$destIP=$result[0];
$sourceIP=$result[1];
$port=$result[2];
$serviceName=$result[3];
$majorCH=$result[4];
$minorCH=$result[5];

# Read PTP time from the PHY
json_decode(shell_exec("$pyth time.py"), true);
$PTP = floatval(file_get_contents("PTP_TIME.dat"));
if (!$PTP) die("Failed loading PTP time");


# Read UTC offset from PTP time with LLS table 0x03 (SystemTime)
$ST = simplexml_load_file("SystemTime.xml");
if (!$ST) die("Failed loading XML file");

#$ST_UTC = intval($ST->SystemTime[0]['currentUtcOffset']);
$ST_UTC = intval($ST['currentUtcOffset']);

chdir('../../bin');
file_put_contents ( "timelog.txt" , "Channel: " . $channel . " \r\n" , FILE_APPEND );
file_put_contents ( "timelog.txt" , "PTP time:" . $PTP . " \r\n" , FILE_APPEND );
file_put_contents ( "timelog.txt" , "Loaded UTC offset:" . $ST_UTC . " \r\n" , FILE_APPEND );
file_put_contents ( "timelog.txt" , "Read from SLT:" . $destIP . " " . $sourceIP . " " . $port . " " . $serviceName . " " . $majorCH . "." . $minorCH . " \r\n" , FILE_APPEND );

$micro_date = microtime();
$date_array = explode(" ",$micro_date);
$date = date("Y-m-d H:i:s",$date_array[1]);
file_put_contents ( "timelog.txt" , "Launching ROUTE:" . $date . $date_array[0] . " \r\n" , FILE_APPEND );

# Start ROUTE receiver
$cmd="$FLUTEReceiver/flute.exe -A -B:$DASHContent -m:$destIP -p:$port -t:0 -E -b:1 -Y:$encodingSymbolsPerPacket -v:0 -J:$Log &";
#$cmd="$FLUTEReceiver/flute.exe -A -B:$DASHContent -m:$destIP -p:$port -t:0 -E -b:0 -Y:$encodingSymbolsPerPacket -v:4 -J:$Log > logout0.txt &";  #Large memory 
if (substr(php_uname(), 0, 7) == "Windows") {
  pclose(popen("start /B ". $cmd, "r"));
} else {
  exec($cmd . " > /dev/null &");
}

sleep(6);

$micro_date = microtime();
$date_array = explode(" ",$micro_date);
$date = date("Y-m-d H:i:s",$date_array[1]);
file_put_contents ( "timelog.txt" , "Start SLS:" . $date . $date_array[0] . " \r\n" , FILE_APPEND );
#while (!glob($DASHContent."/sls")) usleep(5000);
while (!glob($DASHContent."\\"."sls")) usleep(5000);

#Read the content from the envelope file to find contents of SLS
file_put_contents ( "timelog.txt" , "Read Envelope:" . $DASHContent . "\\envelope.xml" . "\r\n" , FILE_APPEND );
$metadataEnvelope = simplexml_load_file("$DASHContent" . "\\" . "envelope.xml");
if (!$metadataEnvelope) die("Failed loading Envelope XML file");
$items = count($metadataEnvelope->item);
for ($i = 0; $i < $items; $i++) {
  $test = $metadataEnvelope->item[$i]['contentType'];
  if ($test == "application/route-usd+xml") { $USBDUri = $metadataEnvelope->item[$i]['metadataURI']; }
  else if ($test == "application/route-s-tsid+xml") { $sTSIDUri = $metadataEnvelope->item[$i]['metadataURI']; }
  else if ($test == "application/dash+xml") { $MPDUri = $metadataEnvelope->item[$i]['metadataURI']; }
  else if ($test == "application/atsc-held+xml") { $HELDUri = $metadataEnvelope->item[$i]['metadataURI']; }
}
#file_put_contents ( "timelog.txt" , "USBD filename '" . $USBDUri . "' S-TSID filename '" . $sTSIDUri . "' MPD filename '" . $MPDUri . "' HELD filename '" . $HELDUri . "' \r\n" , FILE_APPEND );
file_put_contents ( "timelog.txt" , "USBD filename '" . $USBDUri . "' S-TSID filename '" . $sTSIDUri . "' MPD filename '" . $MPDUri . "' \r\n" , FILE_APPEND );

# Read the contents of the USBD file
#while (!glob($DASHContent."/".$USBDUri)) usleep(5000);
while (!glob($DASHContent."\\".$USBDUri)) usleep(5000);
$BundleDescriptionROUTE = simplexml_load_file("$DASHContent" . "\\" . $USBDUri);
if (!$BundleDescriptionROUTE) die("Failed loading USBD file");
$bases = count($BundleDescriptionROUTE->UserServiceDescription[0]->DeliveryMethod->BroadcastAppService->BasePattern);
for ($i = 0; $i < $bases; $i++) {
  $Base[$i] = $BundleDescriptionROUTE->UserServiceDescription[0]->DeliveryMethod->BroadcastAppService->BasePattern[$i];
  $segTemplate[$i] = $Base[$i] . "*.m*";
}
file_put_contents ( "timelog.txt" , "MPD: " . $MPDUri . " \r\n" , FILE_APPEND );
file_put_contents ( "timelog.txt" , "S-TSID: " . $sTSIDUri . " \r\n" , FILE_APPEND );
file_put_contents ( "timelog.txt" , "BaseURL: " . $Base[0] . " \r\n" , FILE_APPEND );

$micro_date = microtime();
$date_array = explode(" ",$micro_date);
$date = date("Y-m-d H:i:s",$date_array[1]);
file_put_contents ( "timelog.txt" , "Start reading MPD:" . $date . $date_array[0] . " \r\n" , FILE_APPEND );
#while (!glob($DASHContent."/".$MPDUri)) usleep(5000);
while (!glob($DASHContent."\\".$MPDUri)) usleep(5000);

#For using with the canned trace file, re-write the AST to current system time when MPD is received
#while (!glob($DASHContent."/".$initAudio)) usleep(5000);
#while (!glob($DASHContent."/".$initVideo)) usleep(5000);
#while (count(glob($DASHContent."/".$segTemplateAudio)) < 3) usleep(5000);
#while (count(glob($DASHContent."/".$segTemplateVideo)) < 3) usleep(5000);



$micro_date = microtime();
$date_array = explode(" ",$micro_date);
$date_array[0] = round($date_array[0],4);
$date = date("Y-m-d H:i:s",$date_array[1]);
file_put_contents ( "timelog.txt" , "Tuned in:" . $date . $date_array[0] . " \r\n" , FILE_APPEND );



$AST_BCST = $PTP - $ST_UTC + $Delay;
file_put_contents ( "timelog.txt" , "Setting AST: " . $AST_BCST . " \r\n" , FILE_APPEND );

$AST_SEC = new DateTime( 'now',  new DateTimeZone( 'UTC' ) );	/* initializer for availability start time */
$AST_SEC->setTimestamp($date_array[1]);    //Better use a single time than now above
#$AST_SEC->add(new DateInterval('PT1S'));
$AST_SEC_W3C = $AST_SEC->format(DATE_W3C);

preg_match('/\.\d*/',$date_array[0],$dateFracPart);
$extension_pos = strrpos($AST_SEC_W3C, '+'); // find position of the last + in W3C date to slip frac seconds
$AST_W3C = substr($AST_SEC_W3C, 0, $extension_pos) . $dateFracPart[0] . "Z" ; //substr($AST_SEC_W3C, $extension_pos);
file_put_contents ( "timelog.txt" , "Setting AST: " . $AST_W3C . " \r\n" , FILE_APPEND );

#$MPD = simplexml_load_file("$DASHContent" . "/" . $MPDUri);
$MPD = simplexml_load_file("$DASHContent" . "\\" . $MPDUri);
if (!$MPD) die("Failed loading XML file");

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
#preg_match('/\.\d*/',$MPD_AST,$matches);
#$fracAST = "0" . $matches[0];
$originalAST = new DateTime($MPD_AST);   
#$deltaTimeASTTuneIn = $AST_SEC->getTimestamp() + round($date_array[0],4) - ($originalAST->getTimestamp() + $fracAST);  //Time elapsed between the original AST and Tune-in time
$deltaTimeASTTuneIn = $AST_SEC->getTimestamp() - $AST_BCST;  //Time elapsed between the original PHY time and Tune-in time


#file_put_contents ( "timelog.txt" , "TimeOffset: " . $deltaTimeASTTuneIn . ", Original time:" . ($originalAST->getTimestamp() + $fracAST) . " Tune-in time: " . ($AST_SEC->getTimestamp() + round($date_array[0],4)) . "\r\n" , FILE_APPEND );
file_put_contents ( "timelog.txt" , "TimeOffset: " . $deltaTimeASTTuneIn . ", Original time:" . ($originalAST->getTimestamp()) . " Updated time: " . ($originalAST->getTimestamp() + $deltaTimeASTTuneIn) . "\r\n" , FILE_APPEND );

$ASTNew = date("Y-m-d H:i:s", ($originalAST->getTimestamp() + $deltaTimeASTTuneIn)) . "Z";
#$MPDNode->setAttribute("availabilityStartTime",date("Y-m-d H:i:s", ($originalAST->getTimestamp() + $deltaTimeASTTuneIn)) . "Z");    //Set AST to tune-in time
$MPDNode->setAttribute("availabilityStartTime",str_replace(" ", "T", $ASTNew));    //Set AST to tune-in time

$responseToSend[1] = count($periods) - 1;

#$dom->save($DASHContent . "/" . $PatchedMPD);
$dom->save($DASHContent . "\\" . $PatchedMPD);

file_put_contents ( "timelog.txt" , "responseToSend Channel: " . $responseToSend[0] . " Period count: " . $responseToSend[1] . "\r\n", FILE_APPEND );


echo json_encode($responseToSend);
echo $PatchedMPD;
#file_put_contents ( "timelog.txt" , $latestFiles , FILE_APPEND );
$micro_date = microtime();
$date_array = explode(" ",$micro_date);
$date = date("Y-m-d H:i:s",$date_array[1]);
file_put_contents ( "timelog.txt" , "Done:" . $date . $date_array[0] . " \r\n" , FILE_APPEND );

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
						if($node->nodeName === 'duration') $durationNode = $node;
						if($node->nodeName === 'timescale') $timescaleNode = $node;
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