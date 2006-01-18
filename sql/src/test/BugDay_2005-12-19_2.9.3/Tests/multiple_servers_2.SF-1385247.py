import os, time, sys

def server_start(x,s,dbinit):
    srvcmd = '%s --dbinit "%s"' % (os.getenv('MSERVER'),dbinit)
    sys.stdout.write('\nserver %d%d : "%s"\n' % (x,s,dbinit))
    sys.stderr.write('\nserver %d%d : "%s"\n' % (x,s,dbinit))
    sys.stdout.flush()
    sys.stderr.flush()
    srv = os.popen(srvcmd, 'w')
    time.sleep(5)                      # give server time to start
    return srv

def server_stop(srv):
    srv.close()

def client(x,s, c, dbinit, lang, cmd, h):
    cltcmd = os.getenv('%s_CLIENT' % lang)
    sys.stdout.write('\nserver %d%d : "%s", client %d: %s%s\n' % (x,s,dbinit,c,h,lang))
    sys.stderr.write('\nserver %d%d : "%s", client %d: %s%s\n' % (x,s,dbinit,c,h,lang))
    sys.stdout.flush()
    sys.stderr.flush()
    clt = os.popen(cltcmd, 'w')
    clt.write(cmd)
    clt.close()
    return '%s(%s) ' % (h,lang)

def clients(x,dbinit):
    s = 0
    s += 1; srv = server_start(x,s,dbinit)
    c = 0 ; h = ''
    c += 1; h = client(x,s,c,dbinit,'SQL' ,'select %d%d%d ;\n' % (x,s,c),h)
    c += 1; h = client(x,s,c,dbinit,'SQL' ,'select %d%d%d ;\n' % (x,s,c),h)
    c += 1; h = client(x,s,c,dbinit,'MAPI', 'print(%d%d%d);\n' % (x,s,c),h)
    c += 1; h = client(x,s,c,dbinit,'MAPI', 'print(%d%d%d);\n' % (x,s,c),h)
    server_stop(srv)
    s += 1; srv = server_start(x,s,dbinit)
    c = 0 ; h = ''
    c += 1; h = client(x,s,c,dbinit,'SQL' ,'select %d%d%d ;\n' % (x,s,c),h)
    c += 1; h = client(x,s,c,dbinit,'MAPI', 'print(%d%d%d);\n' % (x,s,c),h)
    c += 1; h = client(x,s,c,dbinit,'SQL' ,'select %d%d%d ;\n' % (x,s,c),h)
    c += 1; h = client(x,s,c,dbinit,'MAPI', 'print(%d%d%d);\n' % (x,s,c),h)
    server_stop(srv)
    s += 1; srv = server_start(x,s,dbinit)
    c = 0 ; h = ''
    c += 1; h = client(x,s,c,dbinit,'MAPI', 'print(%d%d%d);\n' % (x,s,c),h)
    c += 1; h = client(x,s,c,dbinit,'SQL' ,'select %d%d%d ;\n' % (x,s,c),h)
    c += 1; h = client(x,s,c,dbinit,'SQL' ,'select %d%d%d ;\n' % (x,s,c),h)
    c += 1; h = client(x,s,c,dbinit,'MAPI', 'print(%d%d%d);\n' % (x,s,c),h)
    server_stop(srv)
    s += 1; srv = server_start(x,s,dbinit)
    c = 0 ; h = ''
    c += 1; h = client(x,s,c,dbinit,'SQL' ,'select %d%d%d ;\n' % (x,s,c),h)
    c += 1; h = client(x,s,c,dbinit,'MAPI', 'print(%d%d%d);\n' % (x,s,c),h)
    c += 1; h = client(x,s,c,dbinit,'MAPI', 'print(%d%d%d);\n' % (x,s,c),h)
    c += 1; h = client(x,s,c,dbinit,'SQL' ,'select %d%d%d ;\n' % (x,s,c),h)
    server_stop(srv)
    s += 1; srv = server_start(x,s,dbinit)
    c = 0 ; h = ''
    c += 1; h = client(x,s,c,dbinit,'MAPI', 'print(%d%d%d);\n' % (x,s,c),h)
    c += 1; h = client(x,s,c,dbinit,'SQL' ,'select %d%d%d ;\n' % (x,s,c),h)
    c += 1; h = client(x,s,c,dbinit,'MAPI', 'print(%d%d%d);\n' % (x,s,c),h)
    c += 1; h = client(x,s,c,dbinit,'SQL' ,'select %d%d%d ;\n' % (x,s,c),h)
    server_stop(srv)
    s += 1; srv = server_start(x,s,dbinit)
    c = 0 ; h = ''
    c += 1; h = client(x,s,c,dbinit,'MAPI', 'print(%d%d%d);\n' % (x,s,c),h)
    c += 1; h = client(x,s,c,dbinit,'MAPI', 'print(%d%d%d);\n' % (x,s,c),h)
    c += 1; h = client(x,s,c,dbinit,'SQL' ,'select %d%d%d ;\n' % (x,s,c),h)
    c += 1; h = client(x,s,c,dbinit,'SQL' ,'select %d%d%d ;\n' % (x,s,c),h)
    server_stop(srv)

def main():
    x = 0
    x += 1; clients(x,"module(mapi);module(sql_server); mapi_start(); sql_server_start();")
    x += 1; clients(x,"module(mapi);module(sql_server); sql_server_start(); mapi_start();")
    x += 1; clients(x,"module(sql_server);module(mapi); mapi_start(); sql_server_start();")
    x += 1; clients(x,"module(sql_server);module(mapi); sql_server_start(); mapi_start();")

main()
