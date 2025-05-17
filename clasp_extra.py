Import("env")

print("ClASP Suite integration enabled")
#@.\tools\clasptree.exe .\www .\include\www_content.h /prefix www_ /handlers extended /handlerfsm /block httpd_send_block /expr httpd_send_expr /state resp_arg /epilogue .\include\www_epilogue.h /urlmap www.map
env.Execute("dotnet ./build_tools/clasptree.dll ./www ./include/www_content.h --prefix www_ --epilogue ./include/www_epilogue.h --state resp_arg --block httpd_send_block --expr httpd_send_expr --handlers extended --handlerfsm --urlmap ./www.map")