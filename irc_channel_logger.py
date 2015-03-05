import socket
import sys

server = "irc.freenode.net"       #settings
channel = "#gstreamer"
skip = len(channel)
botnick = "gst1212104822"

irc = socket.socket(socket.AF_INET, socket.SOCK_STREAM) #defines the socket
print "connecting to:" + server
irc.connect((server, 8001))                                                         #connects to the server
# irc.setblocking(False)
irc.send("USER "+ botnick +" "+ botnick +" "+ botnick +" :This is a fun bot!\n") #user authentication
irc.send("NICK "+ botnick +"\n")                            #sets nick
irc.send("PRIVMSG nickserv :iNOOPE\r\n")    #auth
irc.send("JOIN "+ channel +"\n")        #join the chan

while 1:    # puts it in a loop
   text = irc.recv(2040)  #receive the text
   print text   #print text to console

   if text.find('PING') != -1:                          #check if 'PING' is found
      irc.send('PONG ' + text.split() [1] + '\r\n')     #returnes 'PONG' back to the server

   if text.find("PRIVMSG") != -1 and text.find(channel) != -1:
      c = text.find(channel)
      end_of_nick = text.find("!")
      nick = text[1:end_of_nick]
      msg = channel + " | <b>&#60;" + nick + "&#62;</b> " + text[c + skip + 2:]  # formatting
      f = open("irc_messages", "w")
      f.write(msg)
      f.close()
