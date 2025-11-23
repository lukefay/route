<?php

/* 
Clean up processes
 */

exec("sudo killall flute -w");
//exec("taskkill /IM flute.exe /F");
exec("taskkill /F /IM flute.exe /T");

?>