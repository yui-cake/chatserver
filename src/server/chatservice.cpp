#include "chatserivce.hpp"
#include "public.hpp"

#include <muduo/base/Logging.h>
#include <vector>
using namespace std;
using namespace muduo;

ChatSerivce* ChatSerivce::instance()
{
    static ChatSerivce service;
    return &service;
}

ChatSerivce::ChatSerivce()
{
    _msgHandlerMap.insert({LOGIN_MSG, std::bind(&ChatSerivce::login, this, _1, _2, _3)});
    _msgHandlerMap.insert({REG_MSG, std::bind(&ChatSerivce::reg, this, _1, _2, _3)});
    _msgHandlerMap.insert({ONE_CHAT_MSG, std::bind(&ChatSerivce::oneChat, this, _1, _2, _3)});
    _msgHandlerMap.insert({ADD_FRIEND_MSG, std::bind(&ChatSerivce::addFriend, this, _1, _2, _3)});

    _msgHandlerMap.insert({CREATE_GROUP_MSG, std::bind(&ChatSerivce::createGroup, this, _1, _2, _3)});
    _msgHandlerMap.insert({ADD_GROUP_MSG, std::bind(&ChatSerivce::addGroup, this, _1, _2, _3)});
    _msgHandlerMap.insert({GROUP_CHAT_MSG, std::bind(&ChatSerivce::groupChat, this, _1, _2, _3)});
    _msgHandlerMap.insert({LOGINOUT_MSG, std::bind(&ChatSerivce::loginout, this, _1, _2, _3)});

    if(_redis.connect())
    {
        _redis.init_notify_handler(std::bind(&ChatSerivce::handlerRedisSubscribeMessage, this, _1, _2));
    }
}

MsgHandler ChatSerivce::getHandler(int msgid)
{
    //记录错误日志， msgid没有对应的事件处理回调
    auto it = _msgHandlerMap.find(msgid);
    if(it == _msgHandlerMap.end())
    {
        //返回一个默认的处理器，空操作
        return [=](const TcpConnectionPtr &conn, json &js, Timestamp){
            LOG_ERROR << "msgid: " << msgid << " can not find handler!";
        }; 
    }
    else return _msgHandlerMap[msgid];
}


//处理登录业务 ORM 业务层操作的都是对象 DAO 
void ChatSerivce::login(const TcpConnectionPtr &conn, json &js, Timestamp)
{
    int id = js["id"];
    string pwd = js["password"];

    User user = _userModel.query(id);
    if(user.getId() == id && user.getPwd() == pwd)
    {
        if(user.getState() == "online")
        {
            json response;
            response["msgid"] = LOGIN_MSG_ACK;
            response["errno"] = 2;
            response["errmsg"] = "该账号已经登录，请先下线或更换账号";
            conn->send(response.dump());
        }
        else
        {
            {
                //加锁，保证线程安全
                lock_guard<mutex> lock(_connMutex);
                _userConnMap.insert({id, conn});
            }
            //登录成功后，向redis订阅channel
            _redis.subscribe(id); 
            //登录成功，更新用户状态信息
            user.setState("online");
            _userModel.updateState(user);

            json response;
            response["msgid"] = LOGIN_MSG_ACK;
            response["errno"] = 0;
            response["id"] = user.getId();
            response["name"] = user.getName();

            //查询该用户是否有离线消息
            vector<string> vec = _offMsgModel.query(id);
            if(!vec.empty())
            {
                response["offlinemsg"] = vec;
                //读取完后删除
                _offMsgModel.remove(id);
            }

            //查询该用户的好友信息
            vector<User> userVec = _friendModel.query(id);
            if(!userVec.empty())
            {
                vector<string> fvec;
                for(User &user : userVec)
                {
                    json fjs;
                    js["id"] = user.getId();
                    js["name"] = user.getName();
                    js["state"] = user.getState();
                    fvec.push_back(js.dump());
                }
                response["friends"] = fvec;
            }

            //查询该用户群组信息
            vector<Group> groupVec = _groupModel.queryGroups(id);
            if(!groupVec.empty())
            {
                vector<string> gvec;
                for(Group &group : groupVec)
                {
                    json grpjs;
                    grpjs["groupid"] = group.getId();
                    grpjs["groupname"] = group.getName();
                    grpjs["groupdesc"] = group.getDesc();
                    vector<string> uvec;
                    for(GroupUser &user : group.getUsers())
                    {
                        json js;
                        js["id"] = user.getId();
                        js["name"] = user.getName();
                        js["state"] = user.getState();
                        js["role"] = user.getRole();
                        uvec.push_back(js.dump());
                    }
                    grpjs["users"] = uvec;
                    gvec.push_back(grpjs.dump());
                }
                response["groups"] = gvec;
            }

            conn->send(response.dump());
        }
    }
    else
    {
        json response;
        response["msgid"] = LOGIN_MSG_ACK;
        response["errno"] = 1;
        response["errmsg"] = "用户名或密码错误";
        conn->send(response.dump());
    }
}

void ChatSerivce::reg(const TcpConnectionPtr &conn, json &js, Timestamp)
{
    string name = js["name"];
    string pwd = js["password"];

    User user;
    user.setName(name);
    user.setPwd(pwd);
    bool state = _userModel.insert(user);
    if(state)
    {
        //注册成功
        json response;
        response["msgid"] = REG_MSG_ACK;
        response["errno"] = 0;
        response["id"] = user.getId();
        conn->send(response.dump());
    }
    else
    {
        json response;
        response["msgid"] = REG_MSG_ACK;
        response["errno"] = 1;
        conn->send(response.dump());
    }
}

void ChatSerivce::loginout(const TcpConnectionPtr &conn, json &js, Timestamp)
{
    int userid = js["id"];
    {
        lock_guard<mutex> lock(_connMutex);
        auto it = _userConnMap.find(userid);
        if(it != _userConnMap.end())
        {
            _userConnMap.erase(it);
        }
    }

    //取消订阅
    _redis.unsubscribe(userid);

    User user(userid, "", "", "offline");
    _userModel.updateState(user);

    json response;
    response["msgid"] = LOGINOUT_MSG_ACK;
    conn->send(response.dump());
}

void ChatSerivce::oneChat(const TcpConnectionPtr &conn, json &js, Timestamp)
{
    int toid = js["to"].get<int>();
    //查询当前服务器是否在线
    {
        lock_guard<mutex> lock(_connMutex);
        auto it = _userConnMap.find(toid);
        if(it != _userConnMap.end())
        {
            //toid在线，转发消息 服务器主动推送消息给toid用户
            it->second->send(js.dump());
            return;
        }
    }
    
    //查询其他服务器
    User user = _userModel.query(toid);
    if(user.getState() == "online")
    {
        _redis.publish(toid, js.dump());
        return;
    }

    //toid不在线。存储离线消息
    _offMsgModel.insert(toid, js.dump());
}

void ChatSerivce::addFriend(const TcpConnectionPtr &conn, json &js, Timestamp)
{
    int userid = js["id"].get<int>();
    int friendid = js["friendid"].get<int>();

    //存储好友信息
    _friendModel.insert(userid, friendid);
}

//创建群组
void ChatSerivce::createGroup(const TcpConnectionPtr &conn, json &js, Timestamp)
{
    int userid = js["id"].get<int>();
    string name = js["groupname"];
    string desc = js["groupdesc"];

    Group group(-1, name, desc);

    if(_groupModel.createGroup(group))
    {
        _groupModel.addGroup(userid, group.getId(), "creator");
        json response;
        response["errno"] = 0;
        response["msgid"] = CREATE_GROUP_MSG_ACK;
        response["groupid"] = group.getId();
        conn->send(response.dump());
    }

}

//加入群组
void ChatSerivce::addGroup(const TcpConnectionPtr &conn, json &js, Timestamp)
{
    int userid = js["id"].get<int>();
    int groupid = js["groupid"].get<int>();

    _groupModel.addGroup(userid, groupid, "normal");
}


//群聊
void ChatSerivce::groupChat(const TcpConnectionPtr &conn, json &js, Timestamp)
{
    int userid = js["id"].get<int>();
    int groupid = js["groupid"].get<int>();

    vector<int> idVec = _groupModel.queryGroupUsers(userid, groupid);
    lock_guard<mutex> lock(_connMutex);
    for(int id : idVec)
    {
        auto it = _userConnMap.find(id);
        if(it != _userConnMap.end())
        {
            //toid在当前服务器，转发消息 服务器主动推送消息给toid用户
            it->second->send(js.dump());
        }
        else{
            //toid不在当前服务器
            User user = _userModel.query(id);
            if(user.getState() == "online")
            {
                _redis.publish(id, js.dump());
            }
            //toid不在线。存储离线消息
            else _offMsgModel.insert(id, js.dump());
        }
        
    }
}

void ChatSerivce::clientCloseException(const TcpConnectionPtr &conn)
{
    User user;
    {
        lock_guard<mutex> lock(_connMutex);
        for(auto it = _userConnMap.begin(); it != _userConnMap.end(); ++it)
        {
            if(it -> second == conn)
            {
                user.setId(it->first);
                _userConnMap.erase(it);
                break;
            }
        }
    }

    if(user.getId() != -1)
    {
        _redis.unsubscribe(user.getId());
        user.setState("offline");
        _userModel.updateState(user);
    } 
}

void ChatSerivce::reset()
{
    //把online状态的用户，设置成offline
    _userModel.resetState();
}

void ChatSerivce::handlerRedisSubscribeMessage(int userid, string msg)
{
    lock_guard<mutex> lock(_connMutex);
    auto it = _userConnMap.find(userid);
    if(it != _userConnMap.end())
    {
        it->second->send(msg);
        return;
    }

    _offMsgModel.insert(userid, msg);
}
