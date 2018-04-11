#include "myheader.h"



int main(int argc,char *argv[])
    {

        int listen_conn, conn, conn1;
        unsigned int len_clnt;
        struct sockaddr_in servaddr, client1_addr, client2_addr;
        char buffer[TXTLEN];

        int s1_port;


        char txt_in[50], txt_in1[50], clnt1[20], clnt2[20],buffer_time[64];

        int i=0, j=0, len=0, len1=0, n=0, frst_clnt=0, scd_clnt=0, flag=0;

        FILE *outputfile;

        time_t t_srv;
        struct tm *loctime;



        // read the port number and change it to int

        if (argc != 2)
            {
            printf("Invalid String: Input should be filename followed by port number.\n Example: ./server x\n\n");
            return 1;
            }

        else if (atoi(argv[1])==0)
            {
            printf("Invalid port number after server name!\n\n");
            return 1;
            }


        else
         {
                s1_port=atoi(argv[1]);

                //Initialize output file
                outputfile = fopen("chatting.txt", "w");
                fclose(outputfile);

                //Create socket
                listen_conn=socket(AF_INET, SOCK_STREAM, 0);
                if (listen_conn<0)
                    {
                        perror("Socket could not be created!\n");
                        exit(1);
                    }

                //initiating struct values
                servaddr.sin_family=AF_INET;
                servaddr.sin_port = htons(s1_port);
                servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");

                //Bind socket
                if (bind(listen_conn, (struct sockaddr*)&servaddr, sizeof(servaddr))<0)
                    {
                        perror("Socket could not be binded!\n");
                        exit(1);
                    }

                //Listen with x max connection requests
                if(listen(listen_conn, 5)<0)
                    {
                        perror("Error in Listen!\n");

                        exit(1);
                    }


                //Accept Connection
                len_clnt = sizeof(struct sockaddr_in);

                conn=accept(listen_conn, (struct sockaddr*)&client1_addr, &len_clnt);

                if(conn<0)
                  {
                     perror("Error in Accept!\n");
                     exit(1);
                  }

                  else
                    printf("First client Accepted......\n");

                //accept from client 2
                conn1=accept(listen_conn, (struct sockaddr*)&client1_addr, &len_clnt);

                if(conn1<0)
                  {
                     perror("Error in Accept!\n");
                     exit(1);
                  }
                else
                    printf("Second client Accepted......\n");

                //Start
                //empty buffer
                memset(clnt1, 0,sizeof(clnt1));
                memset(clnt2, 0,sizeof(clnt2));

                 do
                    {


                        if (frst_clnt==0)
                            {
                                if (i=read(conn,clnt1,sizeof(clnt1))>0)
                                        {
                                            printf("Username: %s received from client1\n",clnt1);
                                            frst_clnt=frst_clnt+1+flag;
                                            ioctl(conn1, FIONREAD, &len);
                                            if (len>0)
                                                {
                                                        frst_clnt=frst_clnt+1;
                                                        flag=1;
                                                }

                                        }
                            }


                        if (scd_clnt==0)
                            {
                                if (j=read(conn1,clnt2,sizeof(clnt2))>0)
                                        {
                                            printf("Username: %s received from client2\n", clnt2);
                                            scd_clnt=scd_clnt+1;
                                        }
                            }

                        printf("f: %d - s: %d\n",frst_clnt,scd_clnt);
                        if(strcmp(clnt1,clnt2)!=0)
                            {
                                write(conn,"welcome",7);
                                write(conn1,"welcome",7);
                                printf("\nChat initiated!\n\n");
                                n=1;
                                len=0;
                                len1=0;

                            }
                 else
                            {
                                if (frst_clnt>scd_clnt)
                                    {

                                            write(conn,"no",2);
                                            frst_clnt=0;
                                    }


                                if (frst_clnt==scd_clnt)
                                    {

                                        write(conn1,"no",2);
                                        scd_clnt=0;
                                    }

                                printf("Chat not initiated!\n");

                            }


           } while (n==0);


                    if(fork()==0)
                        {

                                //Open file to save log
                                outputfile = fopen("chatting.txt", "a");

                                while (strcmp(txt_in,"exit")!=0||strcmp(txt_in1,"exit")!=0)
                                        {
                                            t_srv=time (NULL);
                                            loctime = localtime (&t_srv);
                                            strftime (buffer_time, 64, "%I:%M:%S %p", loctime);

                                            ioctl(conn, FIONREAD, &len);
                                            if (len>0)
                                                {
                                                    memset(txt_in, 0,sizeof(txt_in));
                                                    i=read(conn,txt_in,sizeof(txt_in));

                                                    if (i<=0)
                                                        {
                                                            printf("Error in Read!\n");
                                                            exit(EXIT_FAILURE);

                                                        }

                                                    write(conn1,txt_in,sizeof(txt_in));
                                                    if (strlen(txt_in)!=1)
                                                        {
                                                            printf("\n%s %s messaged %s with: %s\n",buffer_time,clnt1,clnt2,txt_in);
                                                            fprintf(outputfile, "%s %s messaged %s with: %s\r\n",buffer_time,clnt1,clnt2,txt_in);
                                                            fflush(outputfile);
                                                        }


                                                }


                                            ioctl(conn1, FIONREAD, &len1);
                                            if (len1>0)
                                                {
                                                    memset(txt_in1, 0,sizeof(txt_in1));
                                                    j=read(conn1,txt_in1,sizeof(txt_in1));
                                                    if (j<=0)
                                                        {
                                                            printf("Error in Read!\n");
                                                            exit(EXIT_FAILURE);

                                                        }

                                                    write(conn,txt_in1,sizeof(txt_in1));
                                                    if (strlen(txt_in1)!=1)
                                                            {
                                                                printf("\n%s %s messaged %s with: %s\n",buffer_time,clnt2,clnt1,txt_in1);
                                                                fprintf(outputfile, "%s %s messaged %s with: %s\r\n",buffer_time,clnt2,clnt1,txt_in1);
                                                                fflush(outputfile);
                                                            }


                                                }

                                            if (strcmp(txt_in,"exit")==0||strcmp(txt_in1,"exit")==0||strcmp(txt_in,"exit\n")==0||strcmp(txt_in1,"exit\n")==0)
                                                 {
                                                     printf("\nClient left chat\nSaving chatting.txt\n");
                                                     fclose(outputfile);
                                                     exit(EXIT_SUCCESS);
                                                 }

                                        }

                        }

                    while((strcmp(txt_in,"exit")!=0)||(strcmp(txt_in1,"exit")!=0))
                        {
                            wait(0);
                        }

            close(listen_conn);
            close(conn);
            close(conn1);

            }//else for arguments

    return 0;

    } //for the main
