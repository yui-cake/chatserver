#ifndef USERMODEL_H
#define USERMODEL_H

#include "user.hpp"

class UserModel
{
private:
    
public:
    bool insert(User &user);

    //根据用户id查询用户信息
    User query(int id);
    //更新用户状态信息
    bool updateState(User user);

    //重置用户状态信息
    void resetState();
};


#endif