<?php

/* 
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */
 

header('Content-Type: text/event-stream');
// recommended to prevent caching of event data.
header('Cache-Control: no-cache');

//sleep(1); 

$SLTxml="SLT_signalling/SLT.xml";

$SLT = simplexml_load_file($SLTxml);
if (!$SLT) die("Failed loading XML file");

$dom_sxe = dom_import_simplexml($SLT);
if (!$dom_sxe) {
    echo 'Error while converting XML';
    exit;
}

$dom = new DOMDocument('1.0');
$dom_sxe = $dom->importNode($dom_sxe, true);
$dom_sxe = $dom->appendChild($dom_sxe);

$docElement = $dom->documentElement;

$channels = array();
$serviceIndex = 0;
foreach ($docElement->childNodes as $node) {
	if($node->nodeName === 'SLT') $sltNode = $node;
	if($node->nodeName === 'Service') {
		$Service[]['node'] = $node;
		//$channels[$serviceIndex++]=$serviceIndex; 
		//$channels[$serviceIndex]=$SLT->Service[$serviceIndex++]['shortServiceName'];
		$channels[]=$SLT->Service[$serviceIndex++]['shortServiceName'];
		
		$currentService = &$Service[count($Service) - 1];
		foreach ($currentService['node']->childNodes as $node) {
			if($node->nodeName === 'BroadcastSvcSignaling') {
				$currentService['BroadcastSvcSignaling'][]['node'] = $node;
			}
		}            
	}
}

//$channels = array();
//for ($serviceIndex = 0; $serviceIndex < count($Service); $serviceIndex++) {
	//Loop on all services listed in the SLT
	//$channels[$serviceIndex]=$serviceIndex + 1; 	
	//$channels[$serviceIndex]=$SLT->$Service[$serviceIndex]['minorChannelNo']+0; 
	//$channels[$serviceIndex]=$SLT->$Service[$serviceIndex]->shortServiceName; 
//	$channels[$serviceIndex]=$SLT->bsid; 
	//$channels[$serviceIndex]=$Service[$serviceIndex]['serviceId']; 
//}

echo json_encode($channels);

?>