#!/usr/bin/env python
import socket
import struct
import sys
import time
import gzip

# Set multicast group address 
mcast_addr = '224.0.23.60'
mcast_port = 4937
mcast_group = (mcast_addr, mcast_port)

# Create the socket
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
# Extra option, need to clarify for what is this
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

# Bind to the server address
#sock.bind(mcast_group)
sock.bind(('', mcast_port))

# Tell the operating system to add the socket to the multicast group
# on all interfaces.
mreq = struct.pack("=4sl", socket.inet_aton(mcast_addr), socket.INADDR_ANY)
sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
# set a timeout value of 5 seconds (LLS shall be within 5 seconds)
sock.settimeout(5)

## ***********
## DEBUG
## ***********
#with socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP) as sock:
#	sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)  # make multicast friendly
#	sock.bind(('', mcast_port))
#	# Tell the operating system to add the socket to the multicast group on all interfaces.
#	mreq = struct.pack("=4sl", socket.inet_aton(mcast_addr), socket.INADDR_ANY)
#	sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
#	response = sock.recv(1024) # Read 1024 bytes
#	
#	with open("LLS.dat","wb") as respfile:
#		respfile.write(response)
#	respfile.close()
#	sock.close()

## ***********
## OLD STUFF
## ***********
#
##while True:
#print >>sys.stderr, '\nwaiting to receive message'
#data, address = sock.recvfrom(1024)
#
#print >>sys.stderr, 'received %s bytes from %s' % (len(data), address)
#print >>sys.stderr, data
#
#f = open('../SLT_signalling/SLT.xml', 'w')
#f.write(data)   
#f.close()
#    	
##If need be, use this to send acknowledgement back to the sender.
##print >>sys.stderr, 'sending acknowledgement to', address
##sock.sendto('ack', address)

i = 0
loop = 0
while loop < 6: # there are 6 tables
	respfile = open("LLS.dat","wb")
	response, server = sock.recvfrom(8192)
	#response = sock.recv(8192, 0x40) # 0x40 = MSG_DONTWAIT a.k.a. O_NONBLOCK
	#response = sock.recv(8192)
	respfile.write(response)
	respfile.close()
	
	# Write received information based on table ID
	table = open("LLS.dat", "rb")
	tableID = table.read(1)
	groupID = table.read(1)
	groupCNT = table.read(1)
	tableVer = table.read(1)
	table.close()
	
	# capture received data (not reading first 4 bytes)
	f = open("LLS.dat", "rb")
	data = f.read()[4:]
	# Unzip the LLS
	#data = gzip.decompress(f.read()[4:])
	f.close()
	
	if '{0:08b}'.format(ord(tableID)) == '00000001':
		slt = open("SLT.xml", "wb")
		slt.write(gzip.decompress(data))
		slt.close()
	elif '{0:08b}'.format(ord(tableID)) == '00000010':
		rrt = open("RRT.xml", "wb")
		rrt.write(gzip.decompress(data))
		rrt.close()
	elif '{0:08b}'.format(ord(tableID)) == '00000011':
		systime = open("SystemTime.xml", "wb")
		systime.write(gzip.decompress(data))
		systime.close()
	elif '{0:08b}'.format(ord(tableID)) == '00000100':
		aeat = open("AEAT.xml", "wb")
		aeat.write(gzip.decompress(data))
		aeat.close()
	elif '{0:08b}'.format(ord(tableID)) == '00000101':
		osd = open("OnScreenMessageNotification.xml", "wb")
		osd.write(gzip.decompress(data))
		osd.close()
	elif '{0:08b}'.format(ord(tableID)) == '11111110':
		tables = open("LLS.dat", "rb")
		dummy = tables.read(4)
		tablesCNT = tables.read(1)
		
		# ord command returns integer representing the character (8 bits)
		while i < ord(tablesCNT):
			tablesID = tables.read(1)
			tablesVer = tables.read(1)
			tablesLen = tables.read(1)
			payload = tables.read(ord(tablesLen)*256 + ord(tables.read(1)))
			
			if '{0:08b}'.format(ord(tablesID)) == '00000001':
				slt = open("SLT.xml", "wb")
				slt.write(gzip.decompress(payload))
				slt.close()
			elif '{0:08b}'.format(ord(tablesID)) == '00000010':
				rrt = open("RRT.xml", "wb")
				rrt.write(gzip.decompress(payload))
				rrt.close()
			elif '{0:08b}'.format(ord(tablesID)) == '00000011':
				systime = open("SystemTime.xml", "wb")
				systime.write(gzip.decompress(payload))
				systime.close()
			elif '{0:08b}'.format(ord(tablesID)) == '00000100':
				aeat = open("AEAT.xml", "wb")
				aeat.write(gzip.decompress(payload))
				aeat.close()
			elif '{0:08b}'.format(ord(tablesID)) == '00000101':
				osd = open("OnScreenMessageNotification.xml", "wb")
				osd.write(gzip.decompress(payload))
				osd.close()
			elif '{0:08b}'.format(ord(tablesID)) == '00000110':
				cdt = open("CDT.xml", "wb")
				cdt.write(gzip.decompress(payload))
				cdt.close()
			else:
				meta = open("Metadata.xml", "wb")
				meta.write(gzip.decompress(payload))
				meta.close()
			i += 1
			time.sleep(0.1)
		
		tables.close()
	else:
		meta = open("PUT.xml", "wb")
		meta.write(gzip.decompress(data))
		meta.close()
	loop += 1
	time.sleep(0.1)
sock.close()
