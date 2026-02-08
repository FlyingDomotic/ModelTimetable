#!/usr/bin/python3

# Reformat comments in source code C style and Python files
codeVersion = "25.5.21-1"

import glob
import os
import pathlib
import getopt
import sys
from datetime import datetime

def reformatFile(sourceFile, separator):
    originalFile = sourceFile + datetime.today().strftime('_%Y%m%d_%H%M%S') + ".old"
    tabWidth = 4
    newContent = []
    # Read all lines
    with open(sourceFile, 'rt', encoding = "UTF-8") as inputStream:
        somethingChanged = False
        for line in inputStream.readlines():
            # Remove end of line and trailing spaces
            line = line.rstrip()
            originalLine = line
            # Replace all <tab> by corresponding number of spaces
            try:
                while True:
                    i = line.index( '\t')
                    line = line.replace("\t", " " * (tabWidth - (i % tabWidth)), 1)
            except:
                # Do we have an inline comment?
                if line.find(separator) != -1:
                    # Find first separator not in string
                    inQuote = False
                    for i in range(len(line)):
                        if line[i:i+1] == '"':
                            inQuote = not inQuote
                        if not inQuote and line[i:i+len(separator)] == separator:
                            # We found a separator outside quote
                            startLine = line[:i].rstrip()
                            # Is start of line less than comment column and comment not at start of line, and not multiple comments char
                            if startLine != "" and len(startLine) < commentCol and line[i:i+(len(separator) * 2)] != (separator * 2):
                                # Insert sufficient spaces before comment
                                line = startLine + (" " * (commentCol - (len(startLine)+ 1))) + line[i:]
                            break
                if originalLine != line:
                    somethingChanged = True
                newContent.append(line)
    if somethingChanged:
        print(F"Modifying {sourceFile}")
        # Rename source file
        os.rename(sourceFile, originalFile)
        # Write new source
        with open(sourceFile, 'wt', encoding="UTF-8") as outputStream:
            outputStream.write("\n".join(newContent))

#   *** Main code ***

# Set current working directory to this python file folder
currentPath = pathlib.Path(__file__).parent.resolve()
os.chdir(currentPath)

# Get command file name (without suffix)
cdeFile = pathlib.Path(__file__).stem

inputFiles = []
traceFlag = False
commentCol = 69
cStyleFiles = [".c", ".cpp", ".h", ".hpp", ".ino"]
pythonStyleFiles = [".py"]

# Use command line if given
if sys.argv[1:]:
    command = sys.argv[1:]
else:
    command = []

# Read arguments
helpMsg = 'Usage: ' + cdeFile + ' [options]' + """
    [--input=<input file(s)>]: input file name (can be repeated, default to *.json)
    [--comment=<column number to align comments>]: align end of line comments on this column number
    [--trace]: enable trace
    [--help]: print this help message

    Reformats comments in source code

"""
try:
    opts, args = getopt.getopt(command, "htic=",["help", "trace", "input=", "comment="])
except getopt.GetoptError as excp:
    print(excp.msg)
    print('in >'+str(command)+'<')
    print(helpMsg)
    sys.exit(2)

for opt, arg in opts:
    if opt in ("-h", "--help"):
        print(helpMsg)
        sys.exit()
    elif opt in ("t", "--trace"):
        traceFlag = True
    elif opt in ('i=', '--input'):
        inputFiles.append(arg)
    elif opt in ("c=", "--comment="):
        commentCol = int(arg)

if not inputFiles:
    for extension in cStyleFiles:
        inputFiles.append('*' + extension)
    for extension in pythonStyleFiles:
        inputFiles.append('*' + extension)

if traceFlag:
    print('Input file(s)->'+str(inputFiles))

# Read config files
for specs in inputFiles:
    for sourceFile in glob.glob(specs):
        sourceType = pathlib.Path(sourceFile).suffix
        if sourceType in cStyleFiles:
            reformatFile(sourceFile, "//")
        elif sourceType in pythonStyleFiles:
            reformatFile(sourceFile, "#")
        else:
            print(F"Can't work with {sourceType} type files")
