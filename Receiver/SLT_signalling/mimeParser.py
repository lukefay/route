# -*- coding: utf-8 -*-
#! /usr/bin/env python
"""
A simple script and library to parse out MIME packages to separate files.

Usage: python3 Tools/scripts/mimeParser.py SLS 
"""
#import email.parser
from email import parser
import email
import os, sys

def main(  ):
    if len(sys.argv)==1:
        print("Usage: %s filename" % os.path.basename(sys.argv[0]))
        sys.exit(1)
    
    mailFile = open(sys.argv[1], "r")
    #print(sys.argv[0], sys.argv[1], len(sys.argv))

    #p = email.Parser.Parser(  )
    p = email.parser.Parser(  )
    msg = p.parse(mailFile)
    mailFile.close(  )
    
    body = ""
    
    partCounter = 1
    for part in msg.walk(  ):
        if part.get_content_type(  )=="multipart":
            continue
        ctype = part.get_content_type()
        cdispo = str(part.get('Content-Disposition'))
        file = str(part.get('Content-Location'))
        
        name = part.get_param("name")
        if name==None:
            name = "part-%i" % partCounter
        partCounter+=1
        print(name, file)
        
        # skip any text/plain (txt) attachments
        if ctype == 'text/plain' and 'attachment' not in cdispo:
            body = part.get_payload(decode=True)  # decode
            break
            
        # In real life, make sure that name is a reasonable filename 
        # for your OS; otherwise, mangle it until it is!
        f = open(name,"wb")
        f.write(part.get_payload(decode=True))
        f.close(  )
        print(name)
        

if __name__ == "__main__":
    main(  )
