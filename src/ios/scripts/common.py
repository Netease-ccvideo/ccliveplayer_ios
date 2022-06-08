import sys
import os
import datetime
import logging
import urllib2
import urllib



class StopSignal(Exception):
    pass

def try_mkdir(p):
    if not os.path.isdir(p):
        os.makedirs(p)

abspath = os.path.abspath(__file__)
if abspath.endswith('.pyc'):
    abspath = abspath[:-1]
realpath = os.path.realpath(abspath)
realdir = os.path.dirname(realpath)
SITE_DIR = realdir
CUR_DIR = os.path.dirname(abspath)

ROOT_PATH = os.path.abspath(os.path.join(SITE_DIR, '..'))
print abspath, realpath, realdir, CUR_DIR, ROOT_PATH

LOG_BASE_PATH = os.path.join(CUR_DIR, "log")

if not os.path.isdir(LOG_BASE_PATH):
    try_mkdir(LOG_BASE_PATH)

def get_running_filename():
    return os.path.basename(sys.argv[0])

class LevelFilter(logging.Filter):
    def __init__(self, passlevel, reject):
        self.passlevel = passlevel
        self.reject = reject

    def filter(self, record):
        if self.reject:
            return (record.levelno > self.passlevel)
        else:
            return (record.levelno <= self.passlevel)


def init_log():
    formatter = logging.Formatter('%(asctime)s\t%(levelname)s\t%(message)s\t-\t%(pathname)s:%(lineno)s\t%(funcName)s')
    logger = logging.getLogger("global")
    if hasattr(logger, '_has_init'):
        return logger

    hdlr = logging.FileHandler(os.path.join(LOG_BASE_PATH, "%s.%s.log" % (get_running_filename(), datetime.datetime.now().strftime('%Y-%m'))))
    hdlr.setFormatter(formatter)
    logger.addHandler(hdlr)

    hdlr = logging.FileHandler(os.path.join(LOG_BASE_PATH, "error.%s.log" % (get_running_filename())))
    hdlr.setFormatter(formatter)
    filter = LevelFilter(logging.INFO, True)
    hdlr.addFilter(filter)
    logger.addHandler(hdlr)

    consoleHdlr = logging.StreamHandler(sys.stdout)
    consoleHdlr.setFormatter(formatter)
    logger.addHandler(consoleHdlr)
    logger.setLevel(logging.DEBUG)

    logger._has_init = True
    return logger

global logger
logger = init_log()

def force_int(s):
    try:
        return int(s)
    except:
        return 0

def force_str(s, enc='utf-8'):
    if isinstance(s, basestring):
        return s.encode(enc)

    return str(s)


def do_get(req, encoding='utf-8', timeout=20, use_unicode=0):
    try:
        res = urllib2.urlopen(req, timeout=timeout)
        html = res.read()
        res.close()
        u = html.decode(encoding)
        if not use_unicode:
            u = u.encode('utf-8')
        return True, u
    except urllib2.HTTPError, e:
        return False, "server error:%s %s" % (e.code, e.msg)
    except Exception, e:
        return False, "unknown error:%s" % (repr(e))

if __name__ == '__main__':
    print __file__
    abspath = os.path.abspath(__file__)
    realpath = os.path.realpath(abspath)
    realdir = os.path.dirname(realpath)
    print realdir
    print 'SITE_DIR', SITE_DIR
