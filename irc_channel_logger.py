import signal
import socket
import ssl
import sys
import time

# settings
server = "irc.freenode.net"
port = 8001
channel = "#gstreamer"
botnick = "gst1212104822"

# global
irc = None

def sig_int_handler(signal, frame):
    print("SIGINT: Shutting down nicely...")
    irc.send("QUIT (bye!)")
    sys.exit()

def logline(line):
   line = line.lstrip(":");

   user = line;
   user = user.split("!", 1)[0]

   text = line[line.find('PRIVMSG'):]
   text = text[text.find(':') + 1:]
   text = text.strip(" \t\n\r")

   msg = "<b>&#60;{0}&#62;</b> {1}".format(user, text)

   with open('irc_messages', 'a') as logfile:
      print('[LOGGING] {0}'.format(msg))
      logfile.write ("{0}\n".format(msg))
      logfile.close()

def send(command):
    print('[SENT] {0}'.format(command))
    irc.send("{0}\r\n".format(command))

def runbot():
   global irc

   irc = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
   #irc = ssl.wrap_socket(irc_C)

   irc.connect((server, port))
   irc.setblocking(False)
   send("USER {0} {0} {0} :This a fun bot!".format(botnick))
   send("NICK {0}".format(botnick))
   send("PRIVMSG nickserv :iNOOPE")
   send("JOIN {0}".format(channel))

   while True:
      time.sleep(1)

      try:
         for text in irc.makefile('r'):
            print("[RECEIVED] {0}".format(text.strip()))
            if text.find('PING') != -1:
               send("PONG {0}".format(text.split()[1]))
            if (text.find('PRIVMSG') != -1 and text.find(channel) != -1):
               logline(text)
      except Exception:
         continue

def main():
   signal.signal(signal.SIGINT, sig_int_handler)
   runbot()

if __name__ == "__main__":
   main();
