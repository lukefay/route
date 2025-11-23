<?php
//ini_set('memory_limit','-1');//remove memory limit
/* 
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

if (substr(php_uname(), 0, 7) == "Windows") {
	#chdir("../../bin/");
	$ip=json_decode($_POST['ip']);
	
	#$command= "sudo ../../Config/gensdp.sh ".$ip;
	$command= "bash ../../Config/gensdp.sh $ip";
	$output=array();
	exec($command,$output);
	#var_dump($output);
	
	echo json_encode($ip);
	#echo $ip;
	#echo "done";
} else {
	chdir("../../bin/");
	#$session_id = json_decode($_POST['sessionid']);
	
	$ip=json_decode($_POST['ip']);
	$command= "bash ../../Config/gensdp.sh $ip";
	$output=array();
	exec($command,$output);
	#var_dump($output);
	
	echo json_encode($ip);
	#echo "done";
}
?>
