Step：

1. 给定数据包，提取该数据包的目的IP地址

   注意进行字节序转换

   遍历路由表（链表），dest_ip&&mask==dest&&mask且mask最大

   如果设置默认路由，则肯定能查找到匹配路由条目

   如果查找到相应条目，则将数据包从该条目对应端口转出，否则回复目的网络不可达(ICMP Dest Network Unreachable)


Note: 

1. 增加route表

![1540610331585](C:\Users\Yiwei Li\AppData\Roaming\Typora\typora-user-images\1540610331585.png)

route add default gw 10.0.1.1 dev h1-eth0

route add 'dest_net_ip' gw 'next_hop_ip' dev 'next_hop_interface'