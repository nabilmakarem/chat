#include "myheader.h"



int main(int argc,char *argv[])
    {

        int clnt_socket, i,n=0;

        struct sockaddr_in serv_addr;
        //char clnt_buffer[TXTLEN];

        int c1_port;
        int c1_ip;

        char txt_in[50], nick_name[20],  nick_name_temp[20];

        // read the port number and IP and change it to int
        //c1_ip=atoi(argv[1]);




        if (argc != 3)
            {
            printf("Invalid String: Input should be filename followed by IP and port number.\n Example: ./server 127.0.0.1 x\n\n");
            return 1;
            }

        else if (atoi(argv[2])==0)
            {
            printf("Invalid port number!\n\n");
            return 1;
            }


        else
         {

                 c1_port=atoi(argv[2]);

                //creating socket
                if((clnt_socket = socket(AF_INET, SOCK_STREAM,  IPPROTO_TCP)) < 0)
                    {
                        printf("Socket could not be created!\n");
                        exit(1);
                    }


                memset(&serv_addr, 0, sizeof(serv_addr));
                serv_addr.sin_family = AF_INET;
                serv_addr.sin_port = htons(c1_port);
               //IP specified in the line below

                if(inet_pton(AF_INET, argv[1], &serv_addr.sin_addr)<=0)
                    {
                        printf("Error in IP!\n");
                        exit(1);
                    }

                //Connect to server
                if( connect(clnt_socket, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
                    {
                        printf("\n Error : Connect Failed \n");
                         exit(1);
                    }

                // Reading

            printf("\nEnter your NickName to join the chat - Exit to terminate\n");

           do
           {

                scanf("%s",nick_name);
                strcpy(nick_name_temp, nick_name);
                write(clnt_socket,nick_name,sizeof(nick_name));
                printf("\nwaiting...\n");
                read(clnt_socket, nick_name, sizeof(nick_name));


                if(strcmp(nick_name,"welcome")==0)
                        {
                            printf("\nWelcome %s to the chat!\n\n",nick_name_temp);
                            n=1;

                        }
                 else
                   printf("Please choose another user name.%s is already chosen!\n",nick_name_temp);


           } while (n==0);




            if(fork()==0)
                    {

                         fgets(txt_in,sizeof(txt_in),stdin);


                            while(strcmp(txt_in,"exit")!=0 || strcmp(txt_in,"exit\n")!=0)
                                    {
                                        write(clnt_socket,txt_in,sizeof(txt_in));
                                        printf("%s: ", nick_name_temp);
                                        fgets(txt_in,sizeof(txt_in),stdin);
                                        if (strcmp(txt_in,"exit")==0 || strcmp(txt_in,"exit\n")==0)
                                            {
                                                printf("Leaving the chat....\n");
                                                //exit(EXIT_SUCCESS);
                                                write(clnt_socket,"exit",5);
                                                exit(EXIT_SUCCESS);
                                            }

                                    }

                              close(clnt_socket);


                    }

            else
                    {

                            i=read(clnt_socket,txt_in,sizeof(txt_in));
                            if (i<=0)
                               {
                                        printf("Error in Read!\n");
                                        exit(EXIT_FAILURE);

                                }

                            while(strcmp(txt_in,"exit")!=0 || strcmp(txt_in,"exit\n")!=0)
                                {
                                    if (strcmp(txt_in,"exit")==0 || strcmp(txt_in,"exit\n")==0)
                                            {
                                                printf("Partner left the chat!\n");
                                                //exit(EXIT_SUCCESS);
                                                break;
                                            }
                                    if (strlen(txt_in)!=1)
                                        printf("\nReply: %s\n",txt_in);
                                    i=read(clnt_socket,txt_in,sizeof(txt_in));
                                    if (i<=0)
                                        {
                                        printf("Error in Read!\n");
                                        exit(EXIT_FAILURE);
                                        }
                                }



                            close(clnt_socket);
                    }


            close(clnt_socket);

          } //main else of argument

    return 0;

    } //main
