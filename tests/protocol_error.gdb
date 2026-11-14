set pagination off
set confirm off
break protocol_parse
run
print len
print (char *)body
bt 3
finish
print $rax
disable 1
continue
