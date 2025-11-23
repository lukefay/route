#!/usr/bin/env python
import socket
import struct
import binascii
from datetime import datetime

# Set multicast group address 
mcast_addr = "224.0.1.129"
mcast_port = 8000
ptp = False

ct = datetime.now()
unix_timestamp = ct.timestamp() + 41
with open("PTP_TIME.dat","w") as respfile:
	respfile.write(str(unix_timestamp))
respfile.close()

with socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP) as sock:
	ptp = True
	sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)  # make multicast friendly
	sock.bind(('', mcast_port))
	# Tell the operating system to add the socket to the multicast group on all interfaces.
	mreq = struct.pack("=4sl", socket.inet_aton(mcast_addr), socket.INADDR_ANY)
	sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
	# set a timeout value of 5 seconds ( PTP Time on every PHY Frame)
	sock.settimeout(5)
	response = sock.recv(8) # Read 8 bytes
	#print("received message: %s" % response)
	i1 = binascii.b2a_hex(response[0:1])  # Masked with 0x3F
	i2 = binascii.b2a_hex(response[1:2])
	i3 = binascii.b2a_hex(response[2:3])
	i4 = binascii.b2a_hex(response[3:4])
	i5 = binascii.b2a_hex(response[4:5])
	i5f = binascii.b2a_hex(response[4:5]) # Masked with 0x3F
	i6 = binascii.b2a_hex(response[5:6])
	i6f = binascii.b2a_hex(response[5:6]) # Masked with 0x0F
	i7 = binascii.b2a_hex(response[6:7])
	i7f = binascii.b2a_hex(response[6:7]) # Masked with 0x03
	i8 = binascii.b2a_hex(response[7:8])
	if (int(i1,16) > 191) :
		ib1 = int(i1,16) - 192 # Mask to 0x3F
	else :
		ib1 = int(i1,16)
	ib2 = int(i2,16) # Mask to 0xFF
	ib3 = int(i3,16) # Mask to 0xFF
	ib4 = int(i4,16) # Mask to 0xFF
	ib5 = int(int(i5,16) / 64) # Mask to 0xC0
	if (int(i5f,16) > 191) :
		ib5f = int(i5f,16) - 192 # Mask to 0x3F
	else :
		ib5f = int(i5f,16)
	ib6 = int(int(i6,16) / 16) # Mask to 0xF0
	if (int(i6f,16) > 15) :
		ib6f = int(i6f,16) - 16 # Mask to 0x0F
	else :
		ib6f = int(i6f,16)
	ib7 = int(int(i7,16) / 4) # Mask to 0xFC
	if (int(i7,16) > 3) :
		ib7f = int(i7,16) - 4 # Mask to 0x03
	else :
		ib7f = int(i7,16)
	ib8 = int(i8,16) # Mask to 0xFF	
	#with open("TIME.dat","wb") as respfile:
		#respfile.write(b"PHY PTP Time: " + binascii.b2a_hex(response[:4]) + b"." + binascii.b2a_hex(response[5:8]))
	with open("PTP_TIME.dat","w") as respfile:
		respfile.write(str(ib1*67108864 + ib2*262144 + ib3*1024 + ib4*4 + ib5) + "." + str(ib5f * 16 + ib6) + str(ib6f * 64 + ib7) + str(ib7f * 256 + ib8))
	respfile.close()
	sock.close()
if not ptp:
	ct = datetime.now()
	unix_timestamp = ct.timestamp() + 41
	with open("PTP_TIME.dat","w") as respfile:
		respfile.write(str(unix_timestamp))
	respfile.close()
