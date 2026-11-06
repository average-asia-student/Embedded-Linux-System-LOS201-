set pagination off
set confirm off
set debuginfod enabled off
set architecture arm
set sysroot /home/elaina/embedded_lab3/rootfs/initramfs
target remote 127.0.0.1:1234

break main
break process_data
break /home/elaina/embedded_lab3/apps/buggy_app.c:9 if i == 5
break create_message
break /home/elaina/embedded_lab3/apps/buggy_app.c:18
break calculate

continue
echo \n=== main breakpoint ===\n
bt
info args

continue
echo \n=== process_data breakpoint ===\n
bt
info args
print len

continue
echo \n=== out-of-bounds access: i == len == 5 ===\n
bt
info locals
print i
print len
print &data[5]
print data[5]

continue
echo \n=== create_message breakpoint ===\n
bt
info args

continue
echo \n=== before free(buf) ===\n
info locals
print buf
x/s buf
next
echo \n=== after free(buf) ===\n
print buf
finish
next
print msg
x/s msg

continue
echo \n=== calculate breakpoint ===\n
bt
info args
next
print result
print (long long)a * b
continue
