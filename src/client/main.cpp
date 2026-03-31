#include "json.hpp"
#include <iostream>
#include <thread>
#include <string>
#include <vector>
#include <chrono>
#include <ctime>
#include <functional>
#include <unordered_map>
using namespace std;
using json = nlohmann::json;

#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "group.hpp"
#include "user.hpp"
#include "public.hpp"

//控制聊天页面
bool isOnline = false;
//记录当前系统登陆的用户信息
User g_currentUser;
//记录当前登录用户的好友列表信息
vector<User> g_currentUserFriendList;
//记录当前登录用户的群组列表信息
vector<Group> g_currentUserGroupList;
//显示当前登陆成功用户的基本信息
void showCurrentUserData();

//接收线程
void readTaskHandler(int clientfd);
//获取系统时间
string getCurrentTime();
//主聊天页面程序
void mainMenu(int clientfd);

//聊天客户端程序实现，main线程用作发送线程，子线程用作接收线程
int main(int argc, char **argv)
{
    if(argc < 3)
    {
        cerr << "command invalid! example: ./ChatClient 127.0.0.1 6000" << endl;
        exit(-1);
    }

    char *ip = argv[1];
    uint16_t port = atoi(argv[2]);

    int clientfd = socket(AF_INET, SOCK_STREAM, 0);
    if(-1 == clientfd)
    {
        cerr << "socket create error" << endl;
        exit(-1);
    }

    //填写client需要连接的server信息ip + port
    sockaddr_in server;
    memset(&server, 0, sizeof(sockaddr_in));
    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    server.sin_addr.s_addr = inet_addr(ip);

    //client和server进行连接
    if(-1 == connect(clientfd, (sockaddr *)&server, sizeof(sockaddr_in)))
    {
        cerr << "connect server error" << endl;
        close(clientfd);
        exit(-1);
    }

    for(;;)
    {
        cout << "===================" << endl;
        cout << "1.  login" << endl;
        cout << "2.  register" << endl;
        cout << "3.  quit" << endl;
        cout << "===================" << endl;
        cout << "choice:";
        int choice = 0;
        cin >> choice;
        cin.get();//读掉缓冲区残留的回车

        switch(choice)
        {
            case 1:
            {
                int id = 0;
                char pwd[50] = {0};
                cout << "userid:";
                cin >> id;
                cin.get();
                cout << "userpassword:";
                cin.getline(pwd, 50);

                json js;
                js["msgid"] = LOGIN_MSG;
                js["id"] = id;
                js["password"] = pwd;
                string request = js.dump();

                int len = send(clientfd, request.c_str(), strlen(request.c_str()) + 1, 0);
                if(-1 == len)
                {
                    cerr << "send login msg error:" << request << endl;
                }
                else{
                    char buffer[1024] = {0};
                    len = recv(clientfd, buffer, 1024, 0);
                    if(-1 == len)
                    {
                        cerr << "recv login response error" << endl;
                    }
                    else{
                        json responsejs = json::parse(buffer);
                        if(0 != responsejs["errno"].get<int>())
                        {
                            cerr << responsejs["errmsg"] << endl;
                        }
                        else
                        {
                            g_currentUser.setId(responsejs["id"].get<int>());
                            g_currentUser.setName(responsejs["name"]);

                            if(responsejs.contains("friends"))
                            {
                                g_currentUserFriendList.clear();
                                vector<string> vec = responsejs["friends"];
                                for(string &str : vec)
                                {
                                    json js = json::parse(str);
                                    g_currentUserFriendList.emplace_back(js["id"].get<int>(), js["name"], "", js["state"]);
                                }
                            }

                            if(responsejs.contains("groups"))
                            {
                                g_currentUserGroupList.clear();
                                vector<string> vec = responsejs["groups"];
                                for(string &grpstr : vec)
                                {
                                    json grpjs = json::parse(grpstr);
                                    g_currentUserGroupList.emplace_back(grpjs["groupid"].get<int>(), grpjs["groupname"].get<string>(), grpjs["groupdesc"].get<string>());

                                    vector<string> vec2 = grpjs["users"];
                                    for(string &userstr : vec2)
                                    {
                                        GroupUser user;
                                        json js = json::parse(userstr);
                                        user.setId(js["id"].get<int>());
                                        user.setName(js["name"]);
                                        user.setState(js["state"]);
                                        user.setRole(js["role"]);
                                        g_currentUserGroupList.back().getUsers().push_back(user);
                                    }
                                }
                            }


                            //显示登录用户的基本信息
                            showCurrentUserData();

                            //显示当前用户的离线信息， 个人聊天信息或者群组信息
                            if(responsejs.contains("offlinemsg"))
                            {
                                vector<string> vec = responsejs["offlinemsg"];
                                for(string &str : vec)
                                {
                                    json js = json::parse(str);
                                    if(ONE_CHAT_MSG == js["msgid"].get<int>())
                                    {
                                        cout << js["time"].get<string>() << " [" << js["id"] << "]" << js["name"].get<string>() << " said: " << js["msg"].get<string>() << endl;
                                        
                                    }
                                    else
                                    {
                                        cout << "群消息[" << js["groupid"] << "] " << js["time"].get<string>() << " [" << js["id"] << "]" << js["name"].get<string>() << " said: " << js["msg"].get<string>() << endl;
                                        
                                    }
                                }
                            }
                
                            std::thread readTask(readTaskHandler, clientfd);
                            readTask.detach();

                            isOnline = true;
                            mainMenu(clientfd);
                        }
                    }
                }
            }
            break;
            case 2://register
            {  
                char name[50] = {0};
                char pwd[50] = {0};
                cout << "username:";
                cin.getline(name, 50);
                cout << "userpassword:";
                cin.getline(pwd, 50);

                json js;
                js["msgid"] = REG_MSG;
                js["name"] = name;
                js["password"] = pwd;
                string request = js.dump();

                int len = send(clientfd, request.c_str(), strlen(request.c_str()) + 1, 0);
                if(len == -1)
                {
                    cerr << "send reg msg errer:" << request << endl;
                }
                else
                {
                    char buffer[1024] = {0};
                    len = recv(clientfd, buffer, 1024, 0);
                    if(len == -1)
                    {
                        cerr << "recv reg response error" << endl;
                    }
                    else
                    {
                        json responsejs = json::parse(buffer);
                        if(responsejs["errno"].get<int>() != 0)
                        {
                            cerr << name << " is already exist, register error!" << endl;
                        }
                        else
                        {
                            cout << name << " register success, userid is " << responsejs["id"] << ", do not forget it!" << endl;
                        }
                    }
                }
            }
            break;
            case 3:
                close(clientfd);
                exit(0);
            
            default:
                cerr << "invalid input!" << endl;
                break;
        }
    }
}


//显示当前登陆成功用户的基本信息
void showCurrentUserData()
{
    cout << "===========login user===========" << endl;
    cout << "current login user => id:" << g_currentUser.getId() << " name:" << g_currentUser.getName() << endl;
    cout << "-----------friend list-----------" << endl;
    if(!g_currentUserFriendList.empty())
    {
        for(User &user : g_currentUserFriendList)
        {
            cout << user.getId() << " " << user.getName() << " " << user.getState() << endl;
        }
    }
    cout << "-----------group list-----------" << endl;
    if(!g_currentUserGroupList.empty())
    {
        for(Group &group : g_currentUserGroupList)
        {
            cout << group.getId() << " " << group.getName() << " " << group.getDesc() << endl;
            for(GroupUser &user : group.getUsers())
            {
                cout << user.getId() << " " << user.getName() << " " << user.getState() << " " << user.getRole() << endl;
            }
        }
    }
    cout << "=================================" << endl;
}

//接收线程
void readTaskHandler(int clientfd)
{
    for(;;)
    {
        char buffer[1024] = {0};
        int len = recv(clientfd, buffer, 1024, 0);
        if(-1 == len || 0 == len)
        {
            close(clientfd);
            exit(-1);
        }
        
        
        json js = json::parse(buffer);
        int msgtype = js["msgid"].get<int>();
        if(ONE_CHAT_MSG == msgtype)
        {
            cout << js["time"].get<string>() << " [" << js["id"] << "]" << js["name"].get<string>() << " said: " << js["msg"].get<string>() << endl;
            continue;
        }
        if(GROUP_CHAT_MSG == msgtype)
        {
            cout << "群消息[" << js["groupid"] << "] " << js["time"].get<string>() << " [" << js["id"] << "]" << js["name"].get<string>() << " said: " << js["msg"].get<string>() << endl;
            continue;
        }
        if(CREATE_GROUP_MSG_ACK == msgtype)
        {
            cout << "创建群组成功，群id: " << js["groupid"] << endl;
            continue;
        }
        if(LOGINOUT_MSG_ACK == msgtype)
        {
            break;
        }
    }

}
//获取系统时间
string getCurrentTime()
{
    auto tt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    struct tm *ptm = localtime(&tt);
    char date[60] = {0};
    sprintf(date, "%d-%02d-%02d %02d:%02d:%02d", (int)ptm->tm_year + 1900, (int)ptm->tm_mon + 1, (int)ptm->tm_mday, 
            (int)ptm->tm_hour, (int)ptm->tm_min, (int)ptm->tm_sec);
    return std::string(date);
}

void help(int fd = 0, string str = "");
void chat(int, string);
void addfriend(int, string);
void creategroup(int, string);
void addgroup(int, string);
void groupchat(int, string);
void loginout(int, string);

//系统支持的客户端命令列表
unordered_map<string, string> commandMap = {
    {"help", "显示所有支持的命令，格式help"}, 
    {"chat", "一对一聊天， 格式chat:friendid:message"}, 
    {"addfriend", "添加好友，格式addfriend:friendid"}, 
    {"creategroup", "创建群组，格式creategroup:groupname:groupdesc"}, 
    {"addgroup", "加入群组，格式addgroup:groupid"}, 
    {"groupchat", "群聊，格式groupchat:groupid:message"}, 
    {"loginout", "注销，格式loginout"}
};

//客户端命令对应的处理器
unordered_map<string, function<void(int, string)>> commandHandlerMap = {
    {"help", help},
    {"chat", chat},
    {"addfriend", addfriend},
    {"creategroup", creategroup},
    {"addgroup", addgroup},
    {"groupchat", groupchat},
    {"loginout", loginout}
};



//主聊天页面程序
void mainMenu(int clientfd)
{
    help();

    char buffer[1024] = {0};
    while(isOnline)
    {
        cin.getline(buffer, 1024);
        string commandbuf(buffer);
        string command;
        int idx = commandbuf.find(":");
        if(-1 == idx)
        {
            command = commandbuf;
        }
        else{
            command = commandbuf.substr(0, idx);
        }

        auto it = commandHandlerMap.find(command);
        if(it == commandHandlerMap.end())
        {
            cerr << "invalid input command!" << endl;
            continue;
        }

        it->second(clientfd, commandbuf.substr(idx + 1, commandbuf.size() - idx - 1));
    }
}

void help(int fd, string)
{
    cout << "show command list >>> " << endl;
    for(auto &p : commandMap)
    {
        cout << p.first << " : " << p.second << endl;
    }
    cout << endl;
}

void chat(int clientfd, string str)
{
    int idx = str.find(":");
    if(-1 == idx)
    {
        cerr << "chat command invalid!" << endl;
        return;
    }
    int friendid = atoi(str.substr(0, idx).c_str());
    
    json js;
    js["msgid"] = ONE_CHAT_MSG;
    js["id"] = g_currentUser.getId();
    js["name"] = g_currentUser.getName();
    js["to"] = friendid;
    js["msg"] = str.substr(idx + 1, str.size() - idx - 1);
    js["time"] = getCurrentTime();
    string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if(-1 == len)
    {
        cerr << "send onechat msg error -> " << buffer << endl;
    }

}

void addfriend(int clientfd, string str)
{
    int friendid = atoi(str.c_str());
    json js;
    js["msgid"] = ADD_FRIEND_MSG;
    js["id"] = g_currentUser.getId();
    js["friendid"] = friendid;
    string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if(-1 == len)
    {
        cerr << "send addfriend msg error -> " << buffer << endl;
    }
}

void creategroup(int clientfd, string str) 
{
    int idx = str.find(":");
    if(-1 == idx)
    {
        cerr << "creategroup command invalid!" << endl;
        return; 
    }
    string groupname = str.substr(0, idx);
    string groupdesc = str.substr(idx + 1, str.size() - idx - 1);
    json js;
    js["msgid"] = CREATE_GROUP_MSG;
    js["id"] = g_currentUser.getId();
    js["groupname"] = groupname;
    js["groupdesc"] = groupdesc;

    string buffer = js.dump();
    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if(-1 == len)
    {
        cerr << "send creategroup msg error -> " << buffer << endl;
    }
}

void addgroup(int clientfd, string str)
{
    int groupid = atoi(str.c_str());
    json js;
    js["msgid"] = ADD_GROUP_MSG;
    js["id"] = g_currentUser.getId();
    js["groupid"] = groupid;

    string buffer = js.dump();
    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if(-1 == len)
    {
        cerr << "send addgroup msg error -> " << buffer << endl;
    }
}

void groupchat(int clientfd, string str)
{
    int idx = str.find(":");
    if(-1 == idx)
    {
        cerr << "groupchat command invalid!" << endl;
        return;
    }
    int groupid = atoi(str.substr(0, idx).c_str());
    string message = str.substr(idx + 1, str.size() - idx - 1);

    json js;
    js["msgid"] = GROUP_CHAT_MSG;
    js["id"] = g_currentUser.getId();
    js["name"] = g_currentUser.getName();
    js["groupid"] = groupid;
    js["msg"] = message;
    js["time"] = getCurrentTime();
    string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if(-1 == len)
    {
        cerr << "send groupchat msg error -> " << buffer << endl;
    }
}

void loginout(int clientfd, string str)
{
    json js;
    js["msgid"] = LOGINOUT_MSG;
    js["id"] = g_currentUser.getId();
    string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if(-1 == len)
    {
        cerr << "send loginout msg error -> " << buffer << endl;
    }
    else{
        isOnline = false;
    } 
}