<?php

/* 
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

if (substr(php_uname(), 0, 7) == "Windows") {
	chdir("../../bin/");
	$command3= "bash ../../Config/routeud.sh";
	exec($command3);
} else {
	$command1= "sudo chown -R www-data /var/www/";
	exec($command1);
	$command2="sudo chmod 777 -R /var/www/";
	exec($command2);
	chdir("../../bin/");
	$command3= "bash ../../Config/routeud.sh";
	exec($command3);
}
?>