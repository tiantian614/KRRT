/*
This file is part of ``kdtree'', a library for working with kd-trees.
Copyright (C) 2007-2011 John Tsiombikas <nuclear@member.fsf.org>
Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.
3. The name of the author may not be used to endorse or promote products
   derived from this software without specific prior written permission.
THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
OF SUCH DAMAGE.
*/
/* single nearest neighbor search written by Tamas Nepusz <tamas@cs.rhul.ac.uk> */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "kino_plan/kdtree.h"

#if defined(WIN32) || defined(__WIN32__)
#include <malloc.h>
#endif

#ifdef USE_LIST_NODE_ALLOCATOR

#ifndef NO_PTHREADS
#include <pthread.h>
#else

#ifndef I_WANT_THREAD_BUGS
#error "You are compiling with the fast list node allocator, with pthreads disabled! This WILL break if used from multiple threads."
#endif  /* I want thread bugs */

#endif  /* pthread support */
#endif  /* use list node allocator */

// kdhyperrect是什么结构体？
// 1. 一个结构体，包含了一个int类型的dim，一个double类型的数组min和max
// kdhyperrect 是超平面矩阵 存储了维度 每个维度坐标的最大值和最小值

struct kdhyperrect {
    int dim;
    double *min, *max;              /* minimum/maximum coords */
};

// 这是什么结构体？
// 1. 一个结构体，包含了一个double类型的数组pos，一个int类型的dir，一个void*类型的data，一个指向kdnode的left和right
struct kdnode {
    double *pos;
    int dir;
    void *data;

    struct kdnode *left, *right;    /* negative/positive side */
};

//返回结果节点，包括树的节点，距离值，下一个结果节点的索引。用于节点搜索过程
struct res_node {
    struct kdnode *item;
    double dist_sq;
    struct res_node *next;
};

// 定义一个kd树，包括数据维度，根结点，超平面，销毁data的函数
struct kdtree {
    int dim;
    struct kdnode *root;
    struct kdhyperrect *rect;
    // void (*destr)(void*); 什么意思？
    // 1. 一个函数指针，指向一个函数，函数的参数是void*，返回值是void
    void (*destr)(void*);
};

// kdres是什么结构体？
// 1. 一个结构体，包含了一个指向kdtree的tree，一个指向res_node的rlist和riter，一个int类型的size
struct kdres {
    struct kdtree *tree;
    struct res_node *rlist, *riter;
    int size;
};

// 计算平方的宏定义
#define SQ(x)           ((x) * (x))


static void clear_rec(struct kdnode *node, void (*destr)(void*));
static int insert_rec(struct kdnode **node, const double *pos, void *data, int dir, int dim);
static int rlist_insert(struct res_node *list, struct kdnode *item, double dist_sq);
static void clear_results(struct kdres *set);

static struct kdhyperrect* hyperrect_create(int dim, const double *min, const double *max);
static void hyperrect_free(struct kdhyperrect *rect);
static struct kdhyperrect* hyperrect_duplicate(const struct kdhyperrect *rect);
static void hyperrect_extend(struct kdhyperrect *rect, const double *pos);
static double hyperrect_dist_sq(struct kdhyperrect *rect, const double *pos);

#ifdef USE_LIST_NODE_ALLOCATOR
static struct res_node *alloc_resnode(void);
static void free_resnode(struct res_node*);
#else
#define alloc_resnode()     malloc(sizeof(struct res_node))
#define free_resnode(n)     free(n)
#endif


// 以下函数的作用是什么？
//  1. 创建一个kdtree
// 2. 初始化kdtree的dim, root, destr, rect
// 3. 返回创建的kdtree
// 4. 为kdtree分配内存
//  5. 返回0
//  6. 返回kdtree的dim
// 7. 返回kdtree的root
//  8. 返回kdtree的destr
//  9. 返回kdtree的rect

// 创建k维度的树
struct kdtree *kd_create(int k)
{
    struct kdtree *tree;
    // 为kdtree分配内存
    if(!(tree = malloc(sizeof *tree))) {
        return 0;
    }

    tree->dim = k;
    tree->root = 0;
    tree->destr = 0;
    tree->rect = 0;

    return tree;
}

//以下函数的作用是什么？
//  1. 释放kdtree
// 2. 释放kdtree中的所有节点
// 3. 释放kdtree中的所有hyperrect
//  4. 释放kdtree本身
//  5. 释放kdtree中的所有节点的data
//  6. 释放kdtree中的所有节点的pos
//  7. 释放kdtree中的所有节点的left和right


//释放kdtree
void kd_free(struct kdtree *tree)
{
    if(tree) {
        kd_clear(tree);
        free(tree);
    }
}

//清除超平面，递归方式
static void clear_rec(struct kdnode *node, void (*destr)(void*))
{
    if(!node) return;

    clear_rec(node->left, destr);
    clear_rec(node->right, destr);
    
    if(destr) {
        destr(node->data);
    }
    free(node->pos);
    free(node);
}

//清除kdtree
void kd_clear(struct kdtree *tree)
{
    clear_rec(tree->root, tree->destr);
    tree->root = 0;

    if (tree->rect) {
        hyperrect_free(tree->rect);
        tree->rect = 0;
    }
}

void kd_data_destructor(struct kdtree *tree, void (*destr)(void*))
{
    tree->destr = destr;
}

//在一个节点处插入一个超矩形
static int insert_rec(struct kdnode **nptr, const double *pos, void *data, int dir, int dim)
{
    int new_dir;
    struct kdnode *node;

    if(!*nptr) {
        if(!(node = malloc(sizeof *node))) {
            return -1;
        }
        if(!(node->pos = malloc(dim * sizeof *node->pos))) {
            free(node);
            return -1;
        }
        memcpy(node->pos, pos, dim * sizeof *node->pos);
        node->data = data;
        node->dir = dir;
        node->left = node->right = 0;
        *nptr = node;
        return 0;
    }

    node = *nptr;
    new_dir = (node->dir + 1) % dim;
    if(pos[node->dir] < node->pos[node->dir]) {
        return insert_rec(&(*nptr)->left, pos, data, new_dir, dim);
    }
    return insert_rec(&(*nptr)->right, pos, data, new_dir, dim);
}

//kdtree的插入操作，要插入的位置和数据
int kd_insert(struct kdtree *tree, const double *pos, void *data)
{
    if (insert_rec(&tree->root, pos, data, 0, tree->dim)) {
        return -1;
    }

    if (tree->rect == 0) {
        tree->rect = hyperrect_create(tree->dim, pos, pos);
    } else {
        hyperrect_extend(tree->rect, pos);
    }

    return 0;
}

int kd_insertf(struct kdtree *tree, const float *pos, void *data)
{
    static double sbuf[16];
    double *bptr, *buf = 0;
    int res, dim = tree->dim;

    if(dim > 16) {
#ifndef NO_ALLOCA
        if(dim <= 256)
            bptr = buf = alloca(dim * sizeof *bptr);
        else
#endif
            if(!(bptr = buf = malloc(dim * sizeof *bptr))) {
                return -1;
            }
    } else {
        bptr = buf = sbuf;
    }

    while(dim-- > 0) {
        *bptr++ = *pos++;
    }

    res = kd_insert(tree, buf, data);
#ifndef NO_ALLOCA
    if(tree->dim > 256)
#else
    if(tree->dim > 16)
#endif
        free(buf);
    return res;
}

int kd_insert3(struct kdtree *tree, double x, double y, double z, void *data)
{
    double buf[3];
    buf[0] = x;
    buf[1] = y;
    buf[2] = z;
    return kd_insert(tree, buf, data);
}

int kd_insert3f(struct kdtree *tree, float x, float y, float z, void *data)
{
    double buf[3];
    buf[0] = x;
    buf[1] = y;
    buf[2] = z;
    return kd_insert(tree, buf, data);
}



//找到最近邻的节点  （重点学习   学习函数本身的代码逻辑   以及工程中怎么被引用的    还要关注kd_nearest_range3这个函数  在工程中多次引用）
// 下面函数的作用是什么？
//  1. 递归查找最近的节点
// 2. 返回最近的节点
static int find_nearest(struct kdnode *node, const double *pos, double range, struct res_node *list, int ordered, int dim)
{
    double dist_sq, dx;
    int i, ret, added_res = 0;

    if(!node) return 0;

    dist_sq = 0;
    for(i=0; i<dim; i++) {
        dist_sq += SQ(node->pos[i] - pos[i]);
    }
    if(dist_sq <= SQ(range)) {
        if(rlist_insert(list, node, ordered ? dist_sq : -1.0) == -1) {
            return -1;
        }
        added_res = 1;
    }

    dx = pos[node->dir] - node->pos[node->dir];

    ret = find_nearest(dx <= 0.0 ? node->left : node->right, pos, range, list, ordered, dim);
    if(ret >= 0 && fabs(dx) < range) {
        added_res += ret;
        ret = find_nearest(dx <= 0.0 ? node->right : node->left, pos, range, list, ordered, dim);
    }
    if(ret == -1) {
        return -1;
    }
    added_res += ret;

    return added_res;
}

#if 0
static int find_nearest_n(struct kdnode *node, const double *pos, double range, int num, struct rheap *heap, int dim)
{
    double dist_sq, dx;
    int i, ret, added_res = 0;

    if(!node) return 0;
    
    /* if the photon is close enough, add it to the result heap */
    dist_sq = 0;
    for(i=0; i<dim; i++) {
        dist_sq += SQ(node->pos[i] - pos[i]);
    }
    if(dist_sq <= range_sq) {
        if(heap->size >= num) {
            /* get furthest element */
            struct res_node *maxelem = rheap_get_max(heap);

            /* and check if the new one is closer than that */
            if(maxelem->dist_sq > dist_sq) {
                rheap_remove_max(heap);

                if(rheap_insert(heap, node, dist_sq) == -1) {
                    return -1;
                }
                added_res = 1;

                range_sq = dist_sq;
            }
        } else {
            if(rheap_insert(heap, node, dist_sq) == -1) {
                return =1;
            }
            added_res = 1;
        }
    }


    /* find signed distance from the splitting plane */
    dx = pos[node->dir] - node->pos[node->dir];

    ret = find_nearest_n(dx <= 0.0 ? node->left : node->right, pos, range, num, heap, dim);
    if(ret >= 0 && fabs(dx) < range) {
        added_res += ret;
        ret = find_nearest_n(dx <= 0.0 ? node->right : node->left, pos, range, num, heap, dim);
    }

}
#endif

static void kd_nearest_i(struct kdnode *node, const double *pos, struct kdnode **result, double *result_dist_sq, struct kdhyperrect* rect)
{
    int dir = node->dir;
    int i;
    double dummy, dist_sq;
    struct kdnode *nearer_subtree, *farther_subtree;
    double *nearer_hyperrect_coord, *farther_hyperrect_coord;

    /* Decide whether to go left or right in the tree */
    dummy = pos[dir] - node->pos[dir];
    if (dummy <= 0) {
        nearer_subtree = node->left;
        farther_subtree = node->right;
        nearer_hyperrect_coord = rect->max + dir;
        farther_hyperrect_coord = rect->min + dir;
    } else {
        nearer_subtree = node->right;
        farther_subtree = node->left;
        nearer_hyperrect_coord = rect->min + dir;
        farther_hyperrect_coord = rect->max + dir;
    }

    if (nearer_subtree) {
        /* Slice the hyperrect to get the hyperrect of the nearer subtree */
        dummy = *nearer_hyperrect_coord;
        *nearer_hyperrect_coord = node->pos[dir];
        /* Recurse down into nearer subtree */
        kd_nearest_i(nearer_subtree, pos, result, result_dist_sq, rect);
        /* Undo the slice */
        *nearer_hyperrect_coord = dummy;
    }

    /* Check the distance of the point at the current node, compare it
     * with our best so far */
    dist_sq = 0;
    for(i=0; i < rect->dim; i++) {
        dist_sq += SQ(node->pos[i] - pos[i]);
    }
    if (dist_sq < *result_dist_sq) {
        *result = node;
        *result_dist_sq = dist_sq;
    }

    if (farther_subtree) {
        /* Get the hyperrect of the farther subtree */
        dummy = *farther_hyperrect_coord;
        *farther_hyperrect_coord = node->pos[dir];
        /* Check if we have to recurse down by calculating the closest
         * point of the hyperrect and see if it's closer than our
         * minimum distance in result_dist_sq. */
        if (hyperrect_dist_sq(rect, pos) < *result_dist_sq) {
            /* Recurse down into farther subtree */
            kd_nearest_i(farther_subtree, pos, result, result_dist_sq, rect);
        }
        /* Undo the slice on the hyperrect */
        *farther_hyperrect_coord = dummy;
    }
}

struct kdres *kd_nearest(struct kdtree *kd, const double *pos)
{
    struct kdhyperrect *rect;
    struct kdnode *result;
    struct kdres *rset;
    double dist_sq;
    int i;

    if (!kd) return 0;
    if (!kd->rect) return 0;

    /* Allocate result set */
    if(!(rset = malloc(sizeof *rset))) {
        return 0;
    }
    if(!(rset->rlist = alloc_resnode())) {
        free(rset);
        return 0;
    }
    rset->rlist->next = 0;
    rset->tree = kd;

    /* Duplicate the bounding hyperrectangle, we will work on the copy */
    if (!(rect = hyperrect_duplicate(kd->rect))) {
        kd_res_free(rset);
        return 0;
    }

    /* Our first guesstimate is the root node */
    result = kd->root;
    dist_sq = 0;
    for (i = 0; i < kd->dim; i++)
        dist_sq += SQ(result->pos[i] - pos[i]);

    /* Search for the nearest neighbour recursively */
    kd_nearest_i(kd->root, pos, &result, &dist_sq, rect);

    /* Free the copy of the hyperrect */
    hyperrect_free(rect);

    /* Store the result */
    if (result) {
        if (rlist_insert(rset->rlist, result, -1.0) == -1) {
            kd_res_free(rset);
            return 0;
        }
        rset->size = 1;
        kd_res_rewind(rset);
        return rset;
    } else {
        kd_res_free(rset);
        return 0;
    }
}

struct kdres *kd_nearestf(struct kdtree *tree, const float *pos)
{
    static double sbuf[16];
    double *bptr, *buf = 0;
    int dim = tree->dim;
    struct kdres *res;

    if(dim > 16) {
#ifndef NO_ALLOCA
        if(dim <= 256)
            bptr = buf = alloca(dim * sizeof *bptr);
        else
#endif
            if(!(bptr = buf = malloc(dim * sizeof *bptr))) {
                return 0;
            }
    } else {
        bptr = buf = sbuf;
    }

    while(dim-- > 0) {
        *bptr++ = *pos++;
    }

    res = kd_nearest(tree, buf);
#ifndef NO_ALLOCA
    if(tree->dim > 256)
#else
    if(tree->dim > 16)
#endif
        free(buf);
    return res;
}

struct kdres *kd_nearest3(struct kdtree *tree, double x, double y, double z)
{
    double pos[3];
    pos[0] = x;
    pos[1] = y;
    pos[2] = z;
    return kd_nearest(tree, pos);
}

struct kdres *kd_nearest3f(struct kdtree *tree, float x, float y, float z)
{
    double pos[3];
    pos[0] = x;
    pos[1] = y;
    pos[2] = z;
    return kd_nearest(tree, pos);
}

/* ---- nearest N search ---- */
/*
static kdres *kd_nearest_n(struct kdtree *kd, const double *pos, int num)
{
    int ret;
    struct kdres *rset;
    if(!(rset = malloc(sizeof *rset))) {
        return 0;
    }
    if(!(rset->rlist = alloc_resnode())) {
        free(rset);
        return 0;
    }
    rset->rlist->next = 0;
    rset->tree = kd;
    if((ret = find_nearest_n(kd->root, pos, range, num, rset->rlist, kd->dim)) == -1) {
        kd_res_free(rset);
        return 0;
    }
    rset->size = ret;
    kd_res_rewind(rset);
    return rset;
}*/


// 下面函数的作用是什么？
//  1. 递归查找最近的节点
// 2. 返回最近的节点
struct kdres *kd_nearest_range(struct kdtree *kd, const double *pos, double range)
{
    int ret;
    struct kdres *rset;

    if(!(rset = malloc(sizeof *rset))) {
        return 0;
    }
    if(!(rset->rlist = alloc_resnode())) {
        free(rset);
        return 0;
    }
    rset->rlist->next = 0;
    rset->tree = kd;

    if((ret = find_nearest(kd->root, pos, range, rset->rlist, 0, kd->dim)) == -1) {
        kd_res_free(rset);
        return 0;
    }
    rset->size = ret;
    kd_res_rewind(rset);
    return rset;
}

struct kdres *kd_nearest_rangef(struct kdtree *kd, const float *pos, float range)
{
    static double sbuf[16];
    double *bptr, *buf = 0;
    int dim = kd->dim;
    struct kdres *res;

    if(dim > 16) {
#ifndef NO_ALLOCA
        if(dim <= 256)
            bptr = buf = alloca(dim * sizeof *bptr);
        else
#endif
            if(!(bptr = buf = malloc(dim * sizeof *bptr))) {
                return 0;
            }
    } else {
        bptr = buf = sbuf;
    }

    while(dim-- > 0) {
        *bptr++ = *pos++;
    }

    res = kd_nearest_range(kd, buf, range);
#ifndef NO_ALLOCA
    if(kd->dim > 256)
#else
    if(kd->dim > 16)
#endif
        free(buf);
    return res;
}


//下面函数的作用是什么？
// 1. 递归查找最近的节点
// 2. 返回最近的节点
struct kdres *kd_nearest_range3(struct kdtree *tree, double x, double y, double z, double range)
{
    double buf[3];
    buf[0] = x;
    buf[1] = y;
    buf[2] = z;
    return kd_nearest_range(tree, buf, range);
}

struct kdres *kd_nearest_range3f(struct kdtree *tree, float x, float y, float z, float range)
{
    double buf[3];
    buf[0] = x;
    buf[1] = y;
    buf[2] = z;
    return kd_nearest_range(tree, buf, range);
}

void kd_res_free(struct kdres *rset)
{
    clear_results(rset);
    free_resnode(rset->rlist);
    free(rset);
}

int kd_res_size(struct kdres *set)
{
    return (set->size);
}

void kd_res_rewind(struct kdres *rset)
{
    rset->riter = rset->rlist->next;
}

int kd_res_end(struct kdres *rset)
{
    return rset->riter == 0;
}

int kd_res_next(struct kdres *rset)
{
    rset->riter = rset->riter->next;
    return rset->riter != 0;
}

void *kd_res_item(struct kdres *rset, double *pos)
{
    if(rset->riter) {
        if(pos) {
            memcpy(pos, rset->riter->item->pos, rset->tree->dim * sizeof *pos);
        }
        return rset->riter->item->data;
    }
    return 0;
}

void *kd_res_itemf(struct kdres *rset, float *pos)
{
    if(rset->riter) {
        if(pos) {
            int i;
            for(i=0; i<rset->tree->dim; i++) {
                pos[i] = rset->riter->item->pos[i];
            }
        }
        return rset->riter->item->data;
    }
    return 0;
}

void *kd_res_item3(struct kdres *rset, double *x, double *y, double *z)
{
    if(rset->riter) {
        if(x) *x = rset->riter->item->pos[0];
        if(y) *y = rset->riter->item->pos[1];
        if(z) *z = rset->riter->item->pos[2];
        return rset->riter->item->data;
    }
    return 0;
}

void *kd_res_item3f(struct kdres *rset, float *x, float *y, float *z)
{
    if(rset->riter) {
        if(x) *x = rset->riter->item->pos[0];
        if(y) *y = rset->riter->item->pos[1];
        if(z) *z = rset->riter->item->pos[2];
        return rset->riter->item->data;
    }
    return 0;
}

void *kd_res_item_data(struct kdres *set)
{
    return kd_res_item(set, 0);
}

/* ---- hyperrectangle helpers ---- */
static struct kdhyperrect* hyperrect_create(int dim, const double *min, const double *max)
{
    size_t size = dim * sizeof(double);
    struct kdhyperrect* rect = 0;

    if (!(rect = malloc(sizeof(struct kdhyperrect)))) {
        return 0;
    }

    rect->dim = dim;
    if (!(rect->min = malloc(size))) {
        free(rect);
        return 0;
    }
    if (!(rect->max = malloc(size))) {
        free(rect->min);
        free(rect);
        return 0;
    }
    memcpy(rect->min, min, size);
    memcpy(rect->max, max, size);

    return rect;
}

static void hyperrect_free(struct kdhyperrect *rect)
{
    free(rect->min);
    free(rect->max);
    free(rect);
}

static struct kdhyperrect* hyperrect_duplicate(const struct kdhyperrect *rect)
{
    return hyperrect_create(rect->dim, rect->min, rect->max);
}

static void hyperrect_extend(struct kdhyperrect *rect, const double *pos)
{
    int i;

    for (i=0; i < rect->dim; i++) {
        if (pos[i] < rect->min[i]) {
            rect->min[i] = pos[i];
        }
        if (pos[i] > rect->max[i]) {
            rect->max[i] = pos[i];
        }
    }
}

static double hyperrect_dist_sq(struct kdhyperrect *rect, const double *pos)
{
    int i;
    double result = 0;

    for (i=0; i < rect->dim; i++) {
        if (pos[i] < rect->min[i]) {
            result += SQ(rect->min[i] - pos[i]);
        } else if (pos[i] > rect->max[i]) {
            result += SQ(rect->max[i] - pos[i]);
        }
    }

    return result;
}

/* ---- static helpers ---- */

#ifdef USE_LIST_NODE_ALLOCATOR
/* special list node allocators. */
static struct res_node *free_nodes;

#ifndef NO_PTHREADS
static pthread_mutex_t alloc_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

static struct res_node *alloc_resnode(void)
{
    struct res_node *node;

#ifndef NO_PTHREADS
    pthread_mutex_lock(&alloc_mutex);
#endif

    if(!free_nodes) {
        node = malloc(sizeof *node);
    } else {
        node = free_nodes;
        free_nodes = free_nodes->next;
        node->next = 0;
    }

#ifndef NO_PTHREADS
    pthread_mutex_unlock(&alloc_mutex);
#endif

    return node;
}

static void free_resnode(struct res_node *node)
{
#ifndef NO_PTHREADS
    pthread_mutex_lock(&alloc_mutex);
#endif

    node->next = free_nodes;
    free_nodes = node;

#ifndef NO_PTHREADS
    pthread_mutex_unlock(&alloc_mutex);
#endif
}
#endif  /* list node allocator or not */


/* inserts the item. if dist_sq is >= 0, then do an ordered insert */
/* TODO make the ordering code use heapsort */
//下面函数的作用是什么？
//  1. 插入一个节点
//  2. 如果dist_sq >= 0，按照距离的大小进行排序
static int rlist_insert(struct res_node *list, struct kdnode *item, double dist_sq)
{
    struct res_node *rnode;

    if(!(rnode = alloc_resnode())) {
        return -1;
    }
    rnode->item = item;
    rnode->dist_sq = dist_sq;

    if(dist_sq >= 0.0) {
        while(list->next && list->next->dist_sq < dist_sq) {
            list = list->next;
        }
    }
    rnode->next = list->next;
    list->next = rnode;
    return 0;
}

static void clear_results(struct kdres *rset)
{
    struct res_node *tmp, *node = rset->rlist->next;

    while(node) {
        tmp = node;
        node = node->next;
        free_resnode(tmp);
    }

    rset->rlist->next = 0;
}