import argparse
import zlib

line = 1
firstEmit = 1
indent = ""
eol = ""
output = None
cmdargParser = argparse.ArgumentParser("clstat")
cmdargParser.add_argument("input", help="The input file to generate code for", type=str)
cmdargParser.add_argument("-o","--output", help="The output file to produce", type=str,required=False)
cmdargParser.add_argument("-b","--block", help="The function call to send a literal block to the client", type=str,required=False,default="response_block")
cmdargParser.add_argument("-s", "--state", help = "The variable name that holds the user state to pass to the response functions",required=False,default="response_state",type=str)
cmdargParser.add_argument("-S", "--status", help = "Indicates the HTTP status code to use",required=False,default=200,type=int)
cmdargParser.add_argument("-T", "--text", help = "Indicates the HTTP status text to use",required=False,default="OK",type=str)
cmdargParser.add_argument("-n","--nostatus", required= False, help = "Suppress the status line",action="store_true")
cmdargParser.add_argument("-t","--type", required= False, help = "Indicates the content type of the file. If not specified, it will be based on the file extension",type=str)
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

def processCompression(inpba):
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

def emitText(text):
    global eol
    length = len(text)
    if length > 0:
        data = text.encode("utf-8")
        emit(f"{cmdargs.block}({toSZLiteralBytes(data,len(cmdargs.block)+1)}, {len(data)}, {cmdargs.state});{eol}")

def extToType(ext):
    type = "application/octet-stream"
    match ext:
        case ".aac":
            type = "audio/aac"
        case ".avif":
            type = "image/avif"
        case ".bin":
            type = "application/octet-stream"
        case ".bmp":
            type = "image/bmp"
        case ".css":
            type = "text/css"
        case ".csv":
            type = "text/csv"
        case ".doc":
            type = "application/msword"
        case ".docx":
            type = "application/vnd.openxmlformats-officedocument.wordprocessingml.document"
        case ".epub":
            type = "application/epub+zip"
        case ".gz":
            type = "application/gzip"
        case ".gif":
            type = "image/gif"
        case ".ico":
            type = "image/x-icon"
        case ".jar":
            type = "application/java-archive"
        case ".js":
            type = "text/javascript"
        case ".mjs":
            type = "text/javascript"
        case ".json":
            type = "application/json"
        case ".mid":
            type = "audio/midi"
        case ".midi":
            type = "audio/midi"
        case ".mp3":
            type = "audio/mpeg"
        case ".mp4":
            type = "video/mp4"
        case ".mpeg":
            type = "video/mpeg"
        case ".ogg":
            type = "audio/ogg"
        case ".otf":
            type = "font/otf"
        case ".pdf":
            type = "application/pdf"
        case ".ppt":
            type = "application/vnd.ms-powerpoint"
        case ".pptx":
            type = "application/vnd.openxmlformats-officedocument.presentationml.presentation"
        case ".rar":
            type = "application/vnd.rar"
        case ".rtf":
            type = "application/rtf"
        case ".jpg":
            type = "image/jpeg"
        case ".jpeg":
            type = "image/jpeg"
        case ".png":
            type = "image/png"
        case ".apng":
            type = "image/apng"
        case ".htm":
            type = "text/html"
        case ".html":
            type = "text/html"
        case ".svg":
            type = "image/svg+xml"
        case ".tar":
            type = "application/x-tar"
        case ".tif":
            type = "image/tiff"
        case ".tiff":
            type = "image/tiff"
        case ".ttf":
            type = "font/ttf"
        case ".txt":
            type = "text/plain"
        case ".vsd":
            type = "application/vnd.visio"
        case ".wav":
            type = "audio/wav"
        case ".weba":
            type = "audio/webm"
        case ".webm":
            type = "video/webm"
        case ".webp":
            type = "image/webp"
        case ".woff":
            type = "font/woff"
        case ".woff2":
            type = "font/woff2"
        case ".xhtm":
            type = "application/xhtml+xml"
        case ".xhtml":
            type = "application/xhtml+xml"
        case ".xls":
            type = "application/vnd.ms-excel"        
        case ".xlsx":
            type = "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"
        case ".xml":
            type = "application/xml"
        case ".zip":
            type = "application/zip"
        case ".7z":
            type = "application/x-7z-compressed"
    return type

def isText(type):
    global cmdargs
    if cmdargs.compress != "none" and cmdargs.compress != None:
        return False
    if type.startswith("text/"):
        return True
    match type:
        case "application/vnd.openxmlformats-officedocument.wordprocessingml.document":
            return True
        case "application/json":
            return True
        case "application/vnd.openxmlformats-officedocument.presentationml.presentation":
            return True
        case "application/rtf":
            return True
        case "image/svg+xml":
            return True
        case "application/xhtml+xml":
            return True
        case "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet":
            return True
        case "application/xml":
            return True
    return False

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
    inputFile = open(cmdargs.input,"rb")
    #inputFile = open("espmon.png","rb")
    input = inputFile.read()
    inputLen = len(input)
    inputFile.close()
    mime = "application/octet-stream"
    if cmdargs.type == None:
        exti = cmdargs.input.rfind(".")
        if exti>-1:
            mime = extToType(str(cmdargs.input)[exti:])
    else:
        mime = cmdargs.type

    cmp = processCompression(input)
    headers = ""
    if cmdargs.nostatus == False:
        headers += f"HTTP/1.1 {cmdargs.status} {cmdargs.text}\r\n"
    headers += f"Content-Type: {mime}\r\n"
    if cmp[0] != None:
        headers += f"Content-Encoding: {cmp[0]}\r\n"
    headers += f"Content-Length: {len(cmp[1])}\r\n\r\n"
    for h in headers.split("\r\n")[:-1]:
        emit(f"// {h}{eol}")
    emitDataFieldDecl(headers,cmp[1])
    emit(f"{cmdargs.block}((const char*)http_response_data,sizeof(http_response_data), {cmdargs.state});{eol}")

run()