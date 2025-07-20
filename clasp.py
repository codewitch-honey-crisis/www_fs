import argparse
import zlib
line = 1
firstEmit = 1
indent = ""
eol = ""
output = None
cmdargParser = argparse.ArgumentParser("clasp")
cmdargParser.add_argument("input", help="The input file to generate code for", type=str)
cmdargParser.add_argument("-o","--output", help="The output file to produce", type=str,required=False)
cmdargParser.add_argument("-b","--block", help="The function call to send a literal block to the client", type=str,required=False,default="response_block")
cmdargParser.add_argument("-e", "--expr", help = "The function call to send an expression to the client",type=str,required=False,default="response_expr")
cmdargParser.add_argument("-s", "--state", help = "The variable name that holds the user state to pass to the response functions",required=False,default="response_state",type=str)
cmdargParser.add_argument("-n","--nostatus", required= False, help = "Suppress the status line",action="store_true")
cmdargParser.add_argument("-H","--headers", required= False, help = "Indicates which headers should be generated: auto or none",default="auto",type=str)
cmdargParser.add_argument("-c","--compress", required= False, help = "Indicates the type of compression to use on static content: none, gzip, deflate, or auto.",default="auto",type=str)
cmdargParser.add_argument("-i","--indent", required=False,help = "Indicates the number of spaces to indent each line")
cmdargParser.add_argument("-l","--eol",required=False,default="unix",help="Indicates the style of line ending to use, either \"windows\", \"unix\" or \"apple\"",type=str)
cmdargParser.add_argument("-a","--append", required= False, help = "Append to the output file",action="store_true")

cmdargs = cmdargParser.parse_args()

def deflate_encode(content):
    deflate_compress = zlib.compressobj(9, zlib.DEFLATED, -zlib.MAX_WBITS)
    data = deflate_compress.compress(content) + deflate_compress.flush()
    return data


def gzip_encode(content):
    gzip_compress = zlib.compressobj(9, zlib.DEFLATED, zlib.MAX_WBITS | 16)
    data = gzip_compress.compress(content) + gzip_compress.flush()
    return data

def toSZLiteralBytes(data, startSpacing = 0):
    global eol
    length = len(data)
    result = "\""
    j = startSpacing
    i = 0
    while i<length:
        byte = data[i]
        c = chr(byte)
        if i > 0 and 0 == (j % 80) and i < len(data) - 1:
            result +=(f"\"{eol}    \"")
        if c == '\"':
            result += "\\\""
        elif c == '\\':
            result += "\\\\"
        elif c == '\r':
            result += "\\r"
        elif c == '\n':
            result += "\\n"
        elif c == '\t':
            result += "\\t"
        elif byte >= 32 and byte < 128:
            result += chr(byte)
        else:
            result += "\\x"
            result += format(byte, '02x')
        i = i + 1
        j = j + 1
    result += "\""
    return result

def toSZLiteral(string, startSpacing = 0):
    return toSZLiteralBytes(string.encode("utf-8"),startSpacing)

def generateChunked(resp):
    if resp == None:
        return "0\r\n\r\n"
    if resp == "":
        return ""
    length = len(resp.encode("utf-8"))
    str = format(length,'x')
    str += "\r\n"
    return str + resp + "\r\n"

def emit(content):
    global output
    global indent
    global firstEmit
    global eol
    content = str(content)
    if firstEmit == True:
        firstEmit = False
        content = indent + content
    i = 0
    contentLen = len(content)
    xform = ""
    while i<contentLen:
        ch = content[i]
        if ch=='\r' and i < contentLen-1 and content[i+1]=='\n':
            xform += eol + indent
        elif ch=='\n':
            xform += eol + indent
        else:
            xform += ch
        i += 1
        
    content = xform
    if output == None:
        print(content, end="")
    else:
        output.write(content)

def emitRawResponseBlock(input):
    global eol
    global cmdargs
    if len(input) > 0:
        data = input.encode("utf-8")
        emit(f"{cmdargs.block} ({toSZLiteralBytes(data,len(cmdargs.block))}, {len(data)}, {cmdargs.state});{eol}")

def emitResponseBlock(content):
    global eol
    global cmdargs
    chunked = generateChunked(content)
    if len(chunked) > 0:
        bytes = chunked.encode("utf-8")
        emit(cmdargs.block+"(")
        emit(toSZLiteralBytes(bytes,len(cmdargs.block)+1))
        emit(", ")
        emit(len(bytes))
        emit(", ")
        emit(cmdargs.state)
        emit(f");{eol}")

def emitExpression(expr):
    global eol
    global cmdargs
    emit(cmdargs.expr + "(")
    emit(expr)
    emit(", ")
    emit(cmdargs.state)
    emit(f");{eol}")

def emitCodeBlock(code): 
    global eol
    emit(code+eol)

def hasCodeBlocks(input):
    length = len(input)
    state = 0
    i = 0
    while i<length:
        ch = input[i]
        if state == 0:
            if ch == '<':
                state = 1
        elif state == 1:
            if ch == '%':
                state = 2
            else:
                state = 0
        elif state == 2:
            if ch != '@':
                return True
            state = 0
        i += 1
    return False

def staticLen(input):
    i = input.rfind("%>")
    if i <= 0:
        i = 0
    else: 
        i += 2
    return len(input[i:].encode("utf-8"))

def processCompression(inp):
    inpba = inp.encode("utf-8")
    result = b''
    gzip_data = b''
    defl_data = b''
    type = None
    auto = cmdargs.compress != "deflate" and cmdargs.compress != "none" and cmdargs.compress!="gzip"
    if cmdargs.compress != "none":
        if cmdargs.compress != "deflate":
            gzip_data = gzip_encode(inpba)
        if cmdargs.compress != "gzip":
            defl_data = deflate_encode(inpba)
    else:
        result = inpba
        return (type,result)
    if auto:
        if len(gzip_data) >= len(defl_data):
            cmdargs.compress = "deflate"
            type = "deflate"
            result = defl_data
        else:
            cmdargs.compress = "gzip"
            type = "gzip"
            result = gzip_data
    elif cmdargs.compress == "deflate":
        result = defl_data
    else:
        result = gzip_data
    return (type,result)


def emitDataFieldDecl(prologue, data):
    global eol
    length = len(data)
    emit("static const unsigned char http_response_data[] = {")
    prodata = prologue.encode("utf-8")
    prodatalen = len(prodata)
    i = 0
    while i < prodatalen:
        if (i % 20) == 0:
            emit(eol)
            if i < prodatalen + length - 1:
                emit("    ")
        entry = "0x" + format(prodata[i],'02x')
        if i < prodatalen + length - 1:
            entry += ", "
        emit(entry)
        i = i + 1       
    length += prodatalen
    j = 0
    while i < length:
        if (i % 20) == 0:
            emit(eol)
            if i < length - 1:
                emit("    ")
        entry = "0x" + format(data[j],'02x')
        if i < length - 1:
            entry += ", "
        emit(entry)
        i = i + 1
        j = j + 1
    if (i % 20) != 0:
        emit(" ")
    emit("};"+eol)

def skipSpaces(input, index):
    global line
    length = len(input)
    while index < length:
        ch = input[index]
        if ch!=' ' and ch!='\t' and ch!='\r' and ch!='\n':
            return index
        if ch=='\n':
            line += 1
        index += 1
    return index
    
def readName(input, index):
    length = len(input)
    result = ""
    while index < length:
        ch = input[index]
        if (not ch.isdigit()) and (not ch.isalpha()):
            return (index,result)
        result += ch
        index += 1
    return (index,result)

def readValue(input,index):
    length = len(input)
    quoted = False
    if input[index]=='\"':
        quoted = True
        index += 1
    result = ""
    while index < length:
        ch = input[index]
        if quoted == False:
            if (not ch.isdigit()) and (not ch.isalpha()):
                index = skipSpaces(input,index)
                return (index,result)
        elif ch=='\"':
            index += 1
            index = skipSpaces(input,index)
            return (index,result)
        index += 1
        result += ch
    return (index,result)

def readAttr(input, index):
    index = skipSpaces(input,index)
    namet = readName(input,index)
    index = namet[0]
    name = namet[1]
    index = skipSpaces(input,index)
    if input[index]!='=':
        return (index,name,None)
    index += 1
    index = skipSpaces(input,index)
    valuet = readValue(input,index)
    index = valuet[0]
    value = valuet[1]
    return (index,name,value)

def readContextStart(input, index):
    global line
    length = len(input)
    start = index
    if index>=length:
        return (None,4)
    
    if index<length:
        if input[index]!='<':
            return (index,0)
        index += 1
        if index >= length:
            return (start,0)
        if input[index]!='%':
            return (index-1,0)
        index += 1
        if index >= length:
            return (start,1)
        if input[index]=='@':
            return (index+1,2)
        if input[index]=='=':
            return (index+1,3)
        if input[index]=='\n':
            line += 1
        return (index,1)
    return (start,0)

def readContextRemaining(input, isLiteral, index):
    global line
    length = len(input)
    result = ""
    while index<length:
        if input[index]=='\n':
            line += 1
        if isLiteral:
            if index<length-1 and input[index] == '<' and input[index+1] == '%' :
                return (index,result)
        else:
            if index<length-1 and input[index] == '%' and input[index+1] == '>' :
                return (index+2,result)
        result += input[index]
        index += 1
    return (index,result)

def readContext(input, index):
    startt = readContextStart(input, index)
    if startt[1]<4:
        remt = readContextRemaining(input,startt[1]==0, startt[0])
        return (remt[0],startt[1],remt[1])
    return (index,4,None)

def getHeaders(autoHeaders, isStatic, staticLength, staticCompression,  hasStatus, statusCode, statusText, headers, isChunked):
    emitted = False
    result = ""
    if hasStatus == True:
        result += f"HTTP/1.1 {statusCode} {statusText}\r\n"
        emitted = True
    if len(headers) > 0:
        result += headers
        emitted = True
    if autoHeaders == True:
        if isChunked == None:
            if isStatic == False:
                result += "Transfer-Encoding: chunked\r\n"
                emitted = True
            else:
                if staticCompression != None:
                    result += f"Content-Encoding: {staticCompression}\r\n"
                result += f"Content-Length: {staticLength}\r\n"
                emitted = True

    if emitted == True:
        result += "\r\n"
    return result

def emitHeaders(autoHeaders, isStatic, staticLength, staticCompression, hasStatus, statusCode, statusText, headers, isChunked):
    headers = getHeaders(autoHeaders, isStatic, staticLength, staticCompression, hasStatus, statusCode, statusText, headers, isChunked)
    bytes = headers.encode("utf-8")
    emit(cmdargs.block+"(")
    emit(toSZLiteralBytes(bytes,len(cmdargs.block)+1))
    emit(", ")
    emit(len(bytes))
    emit(", ")
    emit(cmdargs.state)
    emit(");\r\n")

def run():
    global output
    global indent
    global eol
    if cmdargs.eol == "windows":
        eol = "\r\n"
    elif cmdargs.eol == "apple":
        eol = "\r"
    else:
        eol = "\n"

    if cmdargs.indent != None:
        indent = ""
        i = int(cmdargs.indent)
        while i > 0:
            indent += " "
            i -= 1
    if cmdargs.output != None:
        if cmdargs.append == True:
            output = open(cmdargs.output,"a")
        else:
            output = open(cmdargs.output,"w")

    inputFile = open(cmdargs.input,"r")
    statusCode = 200
    statusText = "OK"
    headers = ""
    hasStatus = False
    isChunked = None
    autoHeaders = True
    inDirectives = True
    input = inputFile.read()
    inputLen = len(input)
    inputFile.close()
    isStatic = not hasCodeBlocks(input)
    staticLength = 0
    if isStatic == True:
        staticLength = staticLen(input)
    contextt = None
    index = skipSpaces(input,0)
    while True:
        contextt = readContext(input,index)
        if contextt[1]==4:
            break

        index = contextt[0]
        type = contextt[1]
        content = contextt[2]
        if type == 0 :
            
            if inDirectives == True:
                inDirectives = False
                if isStatic == True:
                    cmp = processCompression(content)
                    headerTmp = getHeaders(autoHeaders,isStatic,len(cmp[1]),cmp[0], hasStatus,statusCode,statusText,headers,isChunked)
                    for h in headerTmp.split("\r\n")[:-1]:
                        emit(f"// {h}{eol}")
                    if cmdargs.headers != "none":
                        emitDataFieldDecl(headerTmp,cmp[1])
                    else:
                        emitDataFieldDecl("",cmp[1])
                    emit(f"{cmdargs.block}((const char*)http_response_data,sizeof(http_response_data), {cmdargs.state});{eol}")
                else:
                    headerTmp = getHeaders(autoHeaders,isStatic,staticLength,None, hasStatus,statusCode,statusText,headers,isChunked)
                    emitRawResponseBlock(headerTmp + generateChunked(content))
            else:
                emitResponseBlock(content)
        elif type == 1:
            if inDirectives == True:
                emitHeaders(autoHeaders,isStatic,staticLength,None, hasStatus,statusCode,statusText,headers,isChunked)
            inDirectives = False
            emitCodeBlock(content)
        elif type == 2:
            # directive
            if inDirectives == False:
                raise Exception(f"Directives must appear before any content on line {line}")
            dirIndex = 0
            namet = readName(content,dirIndex)
            dirIndex = namet[0]    
            if namet[1]=="status":
                hasStatus = True
                while dirIndex < len(content):
                    attrt = readAttr(content,dirIndex)
                    dirIndex = attrt[0]
                    if attrt[1] == "code":
                        statusCode = int(attrt[2])
                    elif attrt[1] == "text":
                        statusText = attrt[2]
                    else:
                        raise Exception(f"Unrecognized attribute \"{attrt[1]}\" on line {line}")
            elif namet[1]=="header":
                name = None
                value = None
                while dirIndex < len(content):
                    attrt = readAttr(content,dirIndex)
                    dirIndex = attrt[0]
                    if attrt[1] == "name":
                        name = attrt[2]
                    elif attrt[1] == "value":
                        value = attrt[2]
                    else:
                        raise Exception(f"Unrecognized attribute \"{attrt[1]}\" on line {line}")
                    
                if name == None:
                    raise Exception(f"Missing \"name\" attribute on line {line}")
                if value == None:
                    raise Exception(f"Missing \"value\" attribute on line {line}")
                if name == "Transfer-Encoding":
                    if value == "chunked":
                        isChunked = True
                    else:
                        isChunked = False
                headers += name +": "+ value + "\r\n"
            else:
                raise Exception(f"Unrecognized directive \"{namet[1]}\" on line {line}")
            index = skipSpaces(input,index)
                
        elif type == 3:
            if inDirectives == True:
                emitHeaders(autoHeaders,isStatic,staticLength,None, hasStatus,statusCode,statusText,headers,isChunked)
            inDirectives = False
            emitExpression(content)
    if isStatic == False:
        emitResponseBlock(None)
    if not (output is None):
        output.close()

run()