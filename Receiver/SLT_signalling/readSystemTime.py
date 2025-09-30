#!/usr/bin/env python
import xml.etree.ElementTree as ET
import os
import time

tree = ET.parse('SystemTime.xml')
SystemTime = tree.getroot()

UTC = SystemTime.get('ptpPrepend')
local = int(UTC) + 1500000
ct = time.ctime(local/1000000)
parsed=time.strptime(ct)
day = time.strftime("%m-%d-%y", parsed)
sec = time.strftime("%T", parsed)

os.system('date %s' % day)
os.system('time %s' % sec)
