def sub(path,old,new,count=1):
    s=open(path,encoding='utf8',newline='').read()
    crlf='\r\n' in s
    if crlf:
        old=old.replace('\r\n','\n').replace('\n','\r\n');new=new.replace('\r\n','\n').replace('\n','\r\n')
    assert s.count(old)==count,(path,old[:80],s.count(old))
    s=s.replace(old,new);open(path,'w',encoding='utf8',newline='').write(s)
