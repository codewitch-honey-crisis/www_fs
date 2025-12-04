# the following is platformIO specific, and despite Import not being
# found by the VS Code LSP, this works. Consider it boilerplate.
Import("env")

print("ClASP Suite integration enabled")

# if you want to use the dotnet version:
# env.Execute("dotnet ./clasptree.dll ./www ./include/www_content.h --prefix www_ --epilogue ./include/www_epilogue.h --state resp_arg --block httpd_send_block --expr httpd_send_expr --handlers extended --handlerfsm --urlmap ./www.map")

# for the python orchestration use this: (do not use both)
env.Execute("python clasptree.py ./www -o ./include/www_content.h -p www_ -E ./include/www_epilogue.h -s resp_arg -b httpd_send_block -e httpd_send_expr -H extended -m -u ./www.map -x www_application.h")