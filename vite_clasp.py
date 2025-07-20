Import("env")
import sys

IS_WINDOWS = sys.platform.startswith("win")
IS_LINUX = sys.platform.startswith("linux")
IS_MAC = sys.platform.startswith("darwin")

print("React+TS integration enabled")
if (IS_WINDOWS):
    env.Execute("build_react.cmd")
else:
    env.Execute("build_react.sh")


print("ClASP Suite integration enabled")

#env.Execute("dotnet ./build_tools/clasptree.dll ./react-web/dist ./common/include/httpd_content.h --prefix httpd_ --epilogue ./common/include/httpd_epilogue.h --state context --block httpd_send_block --expr httpd_send_expr --handlers extended")
env.Execute("python clasptree.py ./react-web/dist -o ./common/include/httpd_content.h -p httpd_ -E ./common/include/httpd_epilogue.h -s context -b httpd_send_block -e httpd_send_expr -H extended")