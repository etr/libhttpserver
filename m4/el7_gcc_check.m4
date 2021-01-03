AC_DEFUN([EL7_GCC_CHECK], [dnl
# Check if we are running Cent 7 / RHEL 7
[ uname -r | grep -w el7 > /dev/null ]
AS_IF([ test $? -eq 0 ],[
# Check for stock compiler, ie gcc 4.8.x
[ $CC -v 2>&1 | grep 4\\.8\\. > /dev/null ]
AS_IF([ test $? -eq 0 ],[
   AS_ECHO(["CentOS / RHEL 7 with the default compiler does not work with this module, please consider using devtoolset"])
   AS_EXIT([status=1])
])
])
])dnl
