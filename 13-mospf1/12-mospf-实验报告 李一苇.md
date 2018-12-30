### <center>第十三周实验报告</center>

#### <center>网络路由实验（第二部分）</center>

<center>2015K8009922021</center>

<center>李一苇</center>

----

**一、实验内容**

​	基于实验一，实现路由器计算路由表项的相关操作



**二、实验流程**

​	在实验一的基础上，得到了一致的链路状态数据库，本次实验据此生成路由表。

​	在实验一dump数据库条目的线程里调用`generate_rib`函数，该函数按照下面顺序依次执行下面各个子命令

1. 将链路状态数据库抽象成图拓扑

   图拓扑的数据结构如下

   ``````c
   #define MAX_NODE_NUM 255
   #define INF 255
   int graph[MAX_NODE_NUM][MAX_NODE_NUM]; //图的邻接矩阵
   int visited[MAX_NODE_NUM]; 
   int dist[MAX_NODE_NUM]; //从图结点0到结点i的最短调跳数
   int prev[MAX_NODE_NUM]; //从图结点0到结点i的最短路的最后一跳结点
   int node_num; //节点数
   u32 dic[MAX_NODE_NUM]; //存储从图的结点i到结点id(dic[i])的映射
   ``````

   - 图拓扑数据结构的初始化`init_graph`

     生成`dic`数组，其中`dic[0] = instance->router_id`代表本节点id

   - 建边`generate_graph`

     注意可以直接建双向边，因为一致链路数据库条目一定是互通的

2. 计算最短路径

   - 在基础的dijkstra算法上，增加记录`prev`数组记录到结点i的最后一跳的图结点编号

3. 生成网络路由

   - 清空已有的路由表（除了从内核读到的本地路由条目，区别在于其网关为`0.0.0.0`）
     - 本质原因在于本实验路由表没有老化机制
   - 按照路径长度从小到大遍历各结点，可用类似dijkstra的第一步选顶点的方法得到最近结点k
   - 计算结点0到结点k的下一跳结点`track_path`
     - 本质是递归prev(k)，直到找到第一跳结点k'
     - 找到从结点0到结点k'的链路，方法：遍历邻居表，找到链路的发送端口`iface`和结点k'的网关`gw`
     - 对结点k的每个子网，填充路由表：(entry->array[i].subnet, entry->array[i].mask) -> (gw, iface)


**三、实验结果和分析**

​       验证方法：

- 运行网络拓扑(topo.py)

  - 在各个路由器节点上执行disable_arp.sh, disable_icmp.sh, disable_ip_forward.sh)，禁止协议栈的相应功能

- 运行./mospfd，使得各个节点生成一致的链路状态数据库，并等待一段时间

  **0. 路由表dump结果**

  ![1543643310692](C:\Users\Yiwei Li\AppData\Roaming\Typora\typora-user-images\1543643310692.png)

  可以看到四个结点的路由表均和预期一致

  **1. 路由互通检测**：在节点h1上ping/traceroute节点h2

  结果如下，正常连通：

![1543641708456](C:\Users\Yiwei Li\AppData\Roaming\Typora\typora-user-images\1543641708456.png)

​	**2. 路由算法健壮性检测**

- 在结点r2上关掉路由程序，等待一个主动LSU发送周期后，重新ping/traceroute r2

![1543643392318](C:\Users\Yiwei Li\AppData\Roaming\Typora\typora-user-images\1543643392318.png)

​	r1的包尝试走r3到r4到达目的地

- 打开结点r2，关掉r3和r4的链路（通过执行`mininet> link r3 r4 down`）

​	![1543643559104](C:\Users\Yiwei Li\AppData\Roaming\Typora\typora-user-images\1543643559104.png)

可以看到在r3到r4的Hello包发送失败（屏幕显示`Send raw packet failed: Network is down`）之后

r3的数据库条目里r4的10.0.5.0已经连不到任何路由器，抽象的拓扑里已经不存在环路了，因而生成的路由表全是从一个接口发送。

