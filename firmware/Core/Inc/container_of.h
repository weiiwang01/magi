//
// Created by weiiwang01 on 9/12/26.
//

#ifndef CONTAINER_OF_H
#define CONTAINER_OF_H

#define container_of(ptr, type, member)                                       \
  ({                                                                          \
    const typeof(((type *)0)->member) *__mptr = (ptr);                        \
    (type *)((char *)__mptr - offsetof(type, member));                        \
  })

#endif // CONTAINER_OF_H
