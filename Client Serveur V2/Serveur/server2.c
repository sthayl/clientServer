#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <time.h>

#include "server2.h"
#include "client2.h"

static void init(void)
{
#ifdef WIN32
   WSADATA wsa;
   int err = WSAStartup(MAKEWORD(2, 2), &wsa);
   if(err < 0)
   {
      puts("WSAStartup failed !");
      exit(EXIT_FAILURE);
   }
#endif
}

static void end(void)
{
#ifdef WIN32
   WSACleanup();
#endif
}

static void app(void)
{
   SOCKET sock = init_connection();
   char buffer[BUF_SIZE];
   /* the index for the array */
   int actual = 0;
   int max = sock;
   /* an array for all clients */
   Client clients[MAX_CLIENTS];

   fd_set rdfs;

   while(1)
   {
      int i = 0;
      FD_ZERO(&rdfs);

      /* add STDIN_FILENO */
      FD_SET(STDIN_FILENO, &rdfs);

      /* add the connection socket */
      FD_SET(sock, &rdfs);

      /* add socket of each client */
      for(i = 0; i < actual; i++)
      {
         FD_SET(clients[i].sock, &rdfs);
      }

      if(select(max + 1, &rdfs, NULL, NULL, NULL) == -1)
      {
         perror("select()");
         exit(errno);
      }

      /* something from standard input : i.e keyboard */
      if(FD_ISSET(STDIN_FILENO, &rdfs))
      {
         /* stop process when type on keyboard */
         break;
      }
      else if(FD_ISSET(sock, &rdfs))
      {
         /* new client */
         SOCKADDR_IN csin = { 0 };
         size_t sinsize = sizeof csin;
         int csock = accept(sock, (SOCKADDR *)&csin, &sinsize);
         if(csock == SOCKET_ERROR)
         {
            perror("accept()");
            continue;
         }

         /* after connecting the client sends its name */
         if(read_client(csock, buffer) == -1)
         {
            /* disconnected */
            continue;
         }

         /* what is the new maximum fd ? */
         max = csock > max ? csock : max;

         FD_SET(csock, &rdfs);

         Client c = { csock };
         strncpy(c.name, buffer, BUF_SIZE - 1);
         clients[actual] = c;
         actual++;
         send_historique_client(clients[actual-1]);
      }
      else
      {
         int i = 0;
         for(i = 0; i < actual; i++)
         {
            /* a client is talking */
            if(FD_ISSET(clients[i].sock, &rdfs))
            {
               time_t t = time(NULL);
               struct tm *tm = localtime(&t);
               char date[64];
               size_t ret = strftime(date, sizeof(date), "%FT%T", tm);

               Client client = clients[i];
               int c = read_client(clients[i].sock, buffer);
               /* client disconnected */
               if(c == 0)
               {
                  closesocket(clients[i].sock);
                  remove_client(clients, i, &actual);
                  strncpy(buffer, client.name, BUF_SIZE - 1);
                  strncat(buffer, " disconnected !", BUF_SIZE - strlen(buffer) - 1);
                  send_message_to_all_clients(clients, client, actual, buffer, 1, date);
               }
               else
               {
                  puts(buffer);
                  char * command;
                  char destinataire[NAME_SIZE];
                  command = split_command_message(destinataire, buffer);
                  if(command[0] == '2')
                  {
                     int nbClient = 0;
                     Client * receiver = get_client(clients, actual, destinataire, &nbClient);
                     if(receiver != NULL)
                     {
                        send_message_to_specific_clients(receiver, client, nbClient, buffer, 0, date);
                        free(receiver);
                     }else
                     {
                        strncpy(buffer, "Le client ", BUF_SIZE - 1);
                        strncat(buffer, destinataire, BUF_SIZE - strlen(buffer) - 1);
                        strncat(buffer, " n'a pas pu être trouvé...", BUF_SIZE - strlen(buffer) - 1);
                        send_message_to_specific_clients(&client, client, 1, buffer, 1, date);
                     }
                  }else
                  {
                     FILE *fptr;
                     fptr = fopen("historique_messages","a");
                     send_message_to_all_clients(clients, client, actual, buffer, 0, date);
                     fprintf(fptr,"%s;%s;ALL;%s;\n", date, client.name, buffer);
                     fclose(fptr);
                  }
                  free(command);
               }
               break;
            }
         }
      }
   }
   clear_clients(clients, actual);
   end_connection(sock);
}

static void clear_clients(Client *clients, int actual)
{
   int i = 0;
   for(i = 0; i < actual; i++)
   {
      closesocket(clients[i].sock);
   }
}

static void remove_client(Client *clients, int to_remove, int *actual)
{
   /* we remove the client in the array */
   memmove(clients + to_remove, clients + to_remove + 1, (*actual - to_remove - 1) * sizeof(Client));
   /* number client - 1 */
   (*actual)--;
}

static void send_message_to_all_clients(Client *clients, Client sender, int actual, const char *buffer, char from_server, char * date)
{
   int i = 0;
   char message[BUF_SIZE];
   message[0] = 0;
   for(i = 0; i < actual; i++)
   {
      /* we don't send message to the sender */
      if(sender.sock != clients[i].sock)
      {
         if(from_server == 0)
         {
            strncpy(message, date, BUF_SIZE - 1);
            strncat(message, " ", sizeof message - strlen(message) - 1);
            strncat(message, sender.name, sizeof message - strlen(message) - 1);
            strncat(message, " : ", sizeof message - strlen(message) - 1);
         }
         strncat(message, buffer, sizeof message - strlen(message) - 1);
         write_client(clients[i].sock, message);
      }
   }
}

static void send_message_to_specific_clients(Client *receivers, Client sender, int nbClients, const char *buffer, int from_server, char * date)
{
   FILE *fptr;
   fptr = fopen("historique_messages","a");

   int i = 0;
   char message[BUF_SIZE];
   message[0] = 0;
   strncpy(message, date, BUF_SIZE - 1);
   for(i = 0; i < nbClients; i++)
   {
      if(from_server)
      {
         strncat(message, " (Server) : ", sizeof message - strlen(message) - 1);
         strncat(message, buffer, sizeof message - strlen(message) - 1);
         write_client(receivers[i].sock, message);
      }else
      {
         strncat(message, " (Message Privé) ", sizeof message - strlen(message) - 1);
         strncat(message, sender.name, sizeof message - strlen(message) - 1);
         strncat(message, " : ", sizeof message - strlen(message) - 1);
         strncat(message, buffer, sizeof message - strlen(message) - 1);
         write_client(receivers[i].sock, message);
         fprintf(fptr,"%s;%s;%s;%s;\n", date, sender.name, receivers[i].name, buffer);
      }
   }
   fclose(fptr);
}

static Client * get_client(Client * listeClients, int sizeListe, char * names, int * nbClient)
{
   Client * clientRetour = NULL;
   char * list_names[MAX_CLIENTS];
   char * token = strtok(names,",");
   int sizeNames=0;
   while (token != NULL) {
      list_names[sizeNames] = (char *)malloc(NAME_SIZE);
      strncpy(list_names[sizeNames], token, NAME_SIZE - 1);
      token = strtok (NULL,",");
      sizeNames++;
   }

   clientRetour = (Client *)malloc(sizeof(Client)*sizeNames);
   *nbClient = 0;
   for(int j=0; j<sizeNames; ++j)
   {
      for(int i=0; i<sizeListe; ++i)
      {
         if(strcmp(listeClients[i].name,list_names[j]) == 0)
         {
            clientRetour[*nbClient] = listeClients[i];
            (*nbClient)++;
            break;
         }
      }
   }
   if(*nbClient == 0){
      free(clientRetour);
      clientRetour = NULL;
   }
   return clientRetour;
}

static char * split_command_message(char * destinataire, char * buffer)
{
   char * token = strtok(buffer, ";");
   char * command = (char *)malloc(sizeof(char));
   strncpy(command, token, 1);
   // command = strtok(buffer, ";");
   token = strtok(NULL, ";");
   strncpy(destinataire, token, NAME_SIZE - 1);
   // destinataire = strtok(NULL, ";");
   token = strtok(NULL, ";");
   strncpy(buffer, token, BUF_SIZE - 1);
   // buffer = strtok(NULL, ";");
   return command;
}

static void send_historique_client(Client client)
{
   FILE *fptr;
   fptr = fopen("historique_messages","r");
   char * buffer = NULL;
   size_t len = BUF_SIZE;
   ssize_t read;
   while (getline(&buffer, &len, fptr) != -1)
   {
      char * date = strtok (buffer, ";" );
      char * sender = strtok(NULL, ";");
      char * receiver = strtok(NULL, ";");
      char * message =  strtok(NULL, ";");
      if(strcmp(receiver, "ALL") == 0 && strcmp(sender, client.name) != 0)
      {
         char toSend[BUF_SIZE];
         toSend[0] = 0;
         strncpy(toSend, date, BUF_SIZE - 1);
         strncat(toSend, " ", sizeof toSend - strlen(toSend) - 1);
         strncat(toSend, sender, sizeof toSend - strlen(toSend) - 1);
         strncat(toSend, " : ", sizeof toSend - strlen(toSend) - 1);
         strncat(toSend, message, sizeof toSend - strlen(toSend) - 1);
         strncat(toSend, "\n", sizeof toSend - strlen(toSend) - 1);
         write_client(client.sock, toSend);
      }else if(strcmp(receiver, client.name) == 0)
      {
         char toSend[BUF_SIZE];
         toSend[0] = 0;
         strncpy(toSend, date, BUF_SIZE - 1);
         strncat(toSend, " (Message privé) ", sizeof toSend - strlen(toSend) - 1);
         strncat(toSend, sender, sizeof toSend - strlen(toSend) - 1);
         strncat(toSend, " : ", sizeof toSend - strlen(toSend) - 1);
         strncat(toSend, message, sizeof toSend - strlen(toSend) - 1);
         strncat(toSend, "\n", sizeof toSend - strlen(toSend) - 1);
         write_client(client.sock, toSend);
      }
    }
   fclose(fptr);
}

static int init_connection(void)
{
   SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
   SOCKADDR_IN sin = { 0 };

   if(sock == INVALID_SOCKET)
   {
      perror("socket()");
      exit(errno);
   }

   sin.sin_addr.s_addr = htonl(INADDR_ANY);
   sin.sin_port = htons(PORT);
   sin.sin_family = AF_INET;

   if(bind(sock,(SOCKADDR *) &sin, sizeof sin) == SOCKET_ERROR)
   {
      perror("bind()");
      exit(errno);
   }

   if(listen(sock, MAX_CLIENTS) == SOCKET_ERROR)
   {
      perror("listen()");
      exit(errno);
   }

   return sock;
}

static void end_connection(int sock)
{
   closesocket(sock);
}

static int read_client(SOCKET sock, char *buffer)
{
   int n = 0;

   if((n = recv(sock, buffer, BUF_SIZE - 1, 0)) < 0)
   {
      perror("recv()");
      /* if recv error we disonnect the client */
      n = 0;
   }

   buffer[n] = 0;

   return n;
}

static void write_client(SOCKET sock, const char *buffer)
{
   if(send(sock, buffer, strlen(buffer), 0) < 0)
   {
      perror("send()");
      exit(errno);
   }
}

int main(int argc, char **argv)
{
   init();

   app();

   end();

   return EXIT_SUCCESS;
}
