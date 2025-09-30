#!/usr/bin/env python
import xml.etree.ElementTree as ET
import sys
import json

serviceId = int(sys.argv[1]) - 1
tree = ET.parse('SLT.xml')
#tree = ET.parse('SLT_signalling/SLT.xml')
SLT  = tree.getroot()
destinationIP = SLT[serviceId][0].get('slsDestinationIpAddress')
destinationPort = SLT[serviceId][0].get('slsDestinationUdpPort')
sourceIP = SLT[serviceId][0].get('slsSourceIpAddress')

majorCH = SLT[serviceId].get('majorChannelNo')
minorCH = SLT[serviceId].get('minorChannelNo')
serviceName = SLT[serviceId].get('shortServiceName')

print(json.dumps([destinationIP, sourceIP, destinationPort, serviceName, majorCH, minorCH]))
