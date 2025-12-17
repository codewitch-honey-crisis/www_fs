# the following is platformIO specific, and despite Import not being
# found by the VS Code LSP, this works. Consider it boilerplate.
Import("env")

print("ClASP Suite integration enabled")

env.Execute("python clasptree.py ./www -o ./include/httpd_content.h -p httpd_ -E ./include/httpd_epilogue.h -s resp_arg -b httpd_send_block -e httpd_send_expr -H extended -m -u ./httpd.map -x httpd_application.h")