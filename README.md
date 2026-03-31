# chatserver
可以工作在nginx tcp负载均衡环境中的集群聊天服务器和客户端源码 基于muduo网络库，redis作为服务器中间件，使用MySQL数据库

编译方式
cd build
rm -rf *
cmake ..
make

运行需要nginx负载均衡模块，muduo网络库，redis,MySQL
