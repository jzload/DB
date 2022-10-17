"""
complexCompare.py for Python

Usage: python complexCompare.py new.xml old.xml diff.html

Examples:
  python complexCompare.py new.xml old.xml diff.html
"""

import sys,getopt
import xml.dom.minidom
import string
import re

def usage():
    print(__doc__)

def getNodeByType(dom):
    measureNodes = dom.getElementsByTagName("measure")
    for measureNode in measureNodes:
        if measureNode.getAttribute("type") == "Function":
            return measureNode
    return None

def getFunctionName(nameAttribute):
    pattern = re.compile("^(.*)\(\.{3}\)")
    fucmatch = pattern.match(nameAttribute)
    if fucmatch:
        return fucmatch.group(1)
    return None

def getFileName(nameAttribute):
    pattern = re.compile("^.*\s+at\s+(\S+):\d+$")
    fucmatch = pattern.match(nameAttribute)
    if fucmatch:
        return fucmatch.group(1)
    return None

def getFunctionComplexity(fileNodes):
    functioncmplx = int(fileNodes.getElementsByTagName("value")[2].firstChild.data)
    return functioncmplx

def getNodeByFunctionName(nodes, funcName):
    for node in nodes:
        if -1 != node.getAttribute("name").find(funcName):
            return node
    return None

def getNodeByNames(nodes, funcName, fileName):
    for node in nodes:
        if -1 != node.getAttribute("name").find(funcName):
            if -1 != node.getAttribute("name").find(fileName):
                return node
    return None

def code_complexity_lizard(newxml, oldxml):
    changeFunctions = []
    xmldomNew = xml.dom.minidom.parse(newxml)
    measureNodeNew = getNodeByType(xmldomNew)
    
    xmldomOld = xml.dom.minidom.parse(oldxml)
    measureNodeOld = getNodeByType(xmldomOld)
    
    newFileNodes = measureNodeNew.getElementsByTagName("item")
    oldFileNodes = measureNodeOld.getElementsByTagName("item")

    if len(newFileNodes) == 0 and len(oldFileNodes):
        print("complex: delete files")
        return None
    
    for filenode in newFileNodes:
        nameAttribute = filenode.getAttribute("name")
        functionName = getFunctionName(nameAttribute)
        newfunctioncmplx = getFunctionComplexity(filenode)

        oldfilenode = getNodeByFunctionName(oldFileNodes,functionName)
        if None != oldfilenode:
            oldfunctioncmplx = getFunctionComplexity(oldfilenode)
            if oldfunctioncmplx != newfunctioncmplx:
                changeFunctions.append([functionName,oldfunctioncmplx,newfunctioncmplx])
            oldFileNodes.remove(oldfilenode)
        else:
            changeFunctions.append([functionName,0,newfunctioncmplx])
        
    return changeFunctions

def code_complexity_lizard_v2(newxml, oldxml):
    changeFunctions = []
    xmldomNew = xml.dom.minidom.parse(newxml)
    measureNodeNew = getNodeByType(xmldomNew)
    
    xmldomOld = xml.dom.minidom.parse(oldxml)
    measureNodeOld = getNodeByType(xmldomOld)
    
    newFileNodes = measureNodeNew.getElementsByTagName("item")
    oldFileNodes = measureNodeOld.getElementsByTagName("item")

    if len(newFileNodes) == 0 and len(oldFileNodes):
        print("complex: delete files")
        return None
    
    for filenode in newFileNodes:
        nameAttribute = filenode.getAttribute("name")
        functionName = getFunctionName(nameAttribute)
        fileName = getFileName(nameAttribute)
        newfunctioncmplx = getFunctionComplexity(filenode)

        oldfilenode = getNodeByNames(oldFileNodes,functionName,fileName)
        if None != oldfilenode:
            oldfunctioncmplx = getFunctionComplexity(oldfilenode)
            if oldfunctioncmplx != newfunctioncmplx:
                changeFunctions.append([functionName+"@"+fileName,oldfunctioncmplx,newfunctioncmplx])
            oldFileNodes.remove(oldfilenode)
        else:
            changeFunctions.append([functionName+"@"+fileName,0,newfunctioncmplx])
        
    return changeFunctions
	
def WriteHtmlHead(file):
    file.write(" <HEAD>\n  <TITLE> Lizard Report </TITLE>\n </HEAD>\n")	
	
def WriteHtmlBegin(file):
    file.write("<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 Transitional//EN\">\n")
    file.write("<HTML>\n")	
	
def WriteHtmlEnd(file):
    file.write("</HTML>\n")	

def WriteBodyBegin(file):
    file.write("<BODY>\n")	
    file.write("<p><b>Diff Code Complexity:</b></p>  \n")
    file.write("<p><b>function[Add]:ccn <= 12</b></p>\n")
    file.write("<p><b>function[Mod]:if(ccn > 12) Increment <= 0</b></p>\n")
    file.write("<table border=\"1\" cellpadding=\"2\" cellspacing=\"0\">\n")
    WriteContextHead(file)
	
def WriteBodyEnd(file):
    file.write("</table>\n")	
    file.write("</BODY>\n")	

def WriteContextHead(file):
    file.write("<tr>\n")
    file.write("<td>SequnceNum</td>\n"
    "<td>FunctionName</td>\n"
    "<td>ComplexityNew</td>\n"
    "<td>ComplexityOld</td>\n"
    "<td>Increment</td>\n"
    "<td>State</td>\n"
    "<td>Accept</td>\n")
    file.write("</tr>\n")
	
def WriteContext(file, num, func):
    state = ''
    accept = 'YES'
    if (func[2] == 0):
        state = 'D'
    elif (func[1] == 0):
        state = 'A'
        if(func[2] > 12):
            accept = 'NO'
    else:
        state = 'M'
        if((func[2] > 12) and (func[2] > func[1])):
            accept = 'NO'
    file.write("<tr>\n")
    file.write("<td>" +  str(num) + "</td>\n")
    file.write("<td>" + func[0] + "</td>\n")
    file.write("<td>" + str(func[2]) + "</td>\n")
    file.write("<td>" + str(func[1]) + "</td>\n")
    file.write("<td>" + str(func[2] - func[1]) + "</td>\n")
    file.write("<td>" + state + "</td>\n")
    file.write("<td>" + accept + "</td>\n")
    file.write("</tr>\n")

def GenToHtml(filepath, funcs):
    try:
        file = open(sys.argv[3],'wt')		
        WriteHtmlBegin(file)
        WriteHtmlHead(file)
        WriteBodyBegin(file)	
        num = 0
        for func in funcs:
            num += 1
            WriteContext(file, num, func)		
        WriteBodyEnd(file)	
        WriteHtmlEnd(file)	
        file.close() 			
    except IOError:
        print("open file error.\n")
		
def main(argv):
    if len(argv) < 3:
        print("complex: len(argv) < 3")
        usage()
        sys.exit()    
    changeFunctions = code_complexity_lizard_v2(sys.argv[1], sys.argv[2])   
    if len(changeFunctions) == 0 :
        print("complex: No change!")
        return		
    GenToHtml(sys.argv[3], changeFunctions)      
if __name__ == "__main__":
    main(sys.argv[1:])







