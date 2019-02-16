# ECE673 Spring 2019 Programming Assignment
The goal of the programming assignment is to explore 
how to apply what we learned in Operating Systems class 
on Process Management to designing high-performance 
web server -- the foundational building block of the WWW.

In this project, we will give you a vanilla server 
which handles one request at a time, and ask you to 
extent it to handle concurrent requests in three ways: 
using multiple processes, using multiple threads, and 
using socket multiplexing (UNIX socket API select()). 

The vanilla server we give you already handles HTTP 
header processing so as to work with Web Polygraph.

## How to run the client?
   The latest version (4.13.0) of Web Polygraph is installed under /etc/polygraph/.  
   Inside /etc/polygraph/bin/, you can find all the binaries generated by Polygraph, including polygraph-client.  

   Before running the specific client for the assignment, you should download and untar ???.
   This will create a ece673_lab1 directory for you. 
   Inside the folder ece673_lab1/client there are three configuration files
   written in Polygraph Language. The root configuration is polymix-1_673.pg.  
   It specifies settings of server and robots (i.e. clients) as well as workload and experiment duration. 
   Ip addresses of server and robots have been declared inside the file. 
   The total number of clients is 10000. We have already helped you set up 10000 IP alias addresses '10.125.1-100.1-100' for you on sp11. 
   You are allowed to customize workload and experiment duration, named Req_Rate and ExpDur respectively.
   Req_Rate is the parameter of the maximum request rate among all the clients. 
   ExpDur is the parameter for running duration. 
   More information like the format of parameters can be accessed through http://www.web-polygraph.org/docs/reference/pgl/.

   To run the client, first make sure you have entered ece673_lab1/client directory. 
   Then run the command following to start the client, 
   /etc/polygraph/bin/polygraph-client --accept_foreign_msgs yes --config polymix-1_673.pg
   There are two necessary options included in the command.
       --accept_foreign_msgs yes: enable polygraph client to work with non-polygraph server. 
       --config polymix-1_673.pg: specify the root configuration file for the client. It will automatically link to other configuration files needed.
   Remember to include those two options each time to run the client.

   Other command options can be found through 
        /etc/polygraph/bin/polygraph-client --help  
    
## How to run the server?  
   The vanilla server is under ece673_lab1/server directory.  
   We have already handled 1) HTTP header processing to work with Polygraph-client; 
   2) for each request, load_content(char *filename, long *filesize) based on the URL; 
   3) responding to the client with HTTP header and also content. 
   The port on the server side is specified as 8888 in the Polygraph configuration file. 

   To run it, first build it through 
   gcc server.c -o server  
   Then start the server via 
   ./server 8888
