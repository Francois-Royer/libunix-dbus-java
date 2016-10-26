/*
 * Java Unix Sockets Library
 *
 * Copyright (c) Matthew Johnson 2005
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation, version 2 only.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * To Contact the author, please email src@matthew.ath.cx
 *
 */


/* _GNU_SOURCE is required to use struct ucred in glibc 2.8 */
#define _GNU_SOURCE
#include "unix-java.h"
#include "jni.h"
#include "linux/jni_md.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <errno.h>
#include <string.h>
#include <sys/un.h>

#ifndef IOV_MAX
#define IOV_MAX 1024
#endif


#ifdef __cplusplus
extern "C" {
#endif

void throw(JNIEnv* env, int err, const char* msg)
{
   jstring jmsg = (*env)->NewStringUTF(env, msg);
   jclass exc = (*env)->FindClass(env, "cx/ath/matthew/unix/UnixIOException");
   jmethodID cons = (*env)->GetMethodID(env, exc, "<init>", "(ILjava/lang/String;)V");
   jobject exo = (*env)->NewObject(env, exc, cons, err, jmsg);
   (*env)->DeleteLocalRef(env, exc);
   (*env)->DeleteLocalRef(env, jmsg);
   (*env)->Throw(env, exo);
   (*env)->DeleteLocalRef(env, exo);
}

void handleerrno(JNIEnv *env)
{
   if (0 == errno) return;
   int err = errno;
   if (EAGAIN == err) return; // we read 0 bytes due to a timeout
   const char* msg = strerror(err);
   throw(env, err, msg);
}

/*
 * Class:     cx_ath_matthew_unix_UnixSocket
 * Method:    native_set_pass_cred
 * Signature: (IZ)V
 */
JNIEXPORT void JNICALL Java_cx_ath_matthew_unix_UnixSocket_native_1set_1pass_1cred
  (JNIEnv *env, jobject o, jint sock, jboolean enable)
{
#ifdef SO_PASSCRED
   int opt = enable;
   int rv = setsockopt(sock, SOL_SOCKET, SO_PASSCRED, &opt, sizeof(int));
   if (-1 == rv) { handleerrno(env);}
#endif
}

/*
 * Class:     cx_ath_matthew_unix_USInputStream
 * Method:    native_recv
 * Signature: ([BII)I
 */
JNIEXPORT jint JNICALL Java_cx_ath_matthew_unix_USInputStream_native_1recv
        (JNIEnv *env, jobject o, jint sock, jbyteArray buf, jint offs, jint len, jint flags, jint timeout)
{
   fd_set rfds;
   struct timeval tv;
   jbyte* cbuf = (*env)->GetByteArrayElements(env, buf, NULL);
   void* recvb = cbuf + offs;
   int rv;

   if (timeout > 0) {
      FD_ZERO(&rfds);
      FD_SET(sock, &rfds);
      tv.tv_sec = 0;
      tv.tv_usec = timeout;
      rv = select(sock+1, &rfds, NULL, NULL, &tv);
      rv = recv(sock, recvb, len, flags);
      if (-1 == rv) { handleerrno(env); rv = -1; }
      (*env)->ReleaseByteArrayElements(env, buf, cbuf, 0);
      return rv;
   } else  {
      rv = recv(sock, recvb, len, flags);
      (*env)->ReleaseByteArrayElements(env, buf, cbuf, 0);
      if (-1 == rv) { handleerrno(env); return -1; }
      return rv;
   }
}

/*
 * Class:     cx_ath_matthew_unix_USOutputStream
 * Method:    native_send
 * Signature: (I[BII)I
 */
JNIEXPORT jint JNICALL Java_cx_ath_matthew_unix_USOutputStream_native_1send__I_3BII
  (JNIEnv *env, jobject o, jint sock, jbyteArray buf, jint offs, jint len)
{
   jbyte* cbuf = (*env)->GetByteArrayElements(env, buf, NULL);
   void* sendb = cbuf + offs;
   int rv = send(sock, sendb, len, 0);
   (*env)->ReleaseByteArrayElements(env, buf, cbuf, 0);
   if (-1 == rv) { handleerrno(env); return -1; }
   return rv;
}

/*
 * Class:     cx_ath_matthew_unix_USOutputStream
 * Method:    native_send
 * Signature: (I[[B)I
 */
JNIEXPORT jint JNICALL Java_cx_ath_matthew_unix_USOutputStream_native_1send__I_3_3B
  (JNIEnv *env, jobject o, jint sock, jobjectArray bufs)
{
   size_t sblen = 1;
   socklen_t sblen_size = sizeof(sblen);
   getsockopt(sock, SOL_SOCKET, SO_SNDBUF, &sblen, &sblen_size);

   struct msghdr msg;
   struct iovec *iov;
   msg.msg_name = NULL;
   msg.msg_namelen = 0;
   msg.msg_control = NULL;
   msg.msg_controllen = 0;
   msg.msg_flags = 0;
   size_t els = (*env)->GetArrayLength(env, bufs);
   iov = (struct iovec*) malloc((els<IOV_MAX?els:IOV_MAX) * sizeof(struct iovec));
   msg.msg_iov = iov;
   jbyteArray *b = (jbyteArray*) malloc(els * sizeof(jbyteArray));
   int rv = 0;

   for (int i = 0, j = 0, s = 0; i <= els; i++, j++) {
      if (i == els) {
         msg.msg_iovlen = j;
         rv = sendmsg(sock, &msg, 0);
         for (int k = i-1, l = j-1; l >= 0; k--, l--)
            (*env)->ReleaseByteArrayElements(env, b[k], iov[l].iov_base, 0);
         if (-1 == rv) { handleerrno(env); return -1; }
         break;
      }
      b[i] = (*env)->GetObjectArrayElement(env, bufs, i);
      if (NULL == b[i]) {
         msg.msg_iovlen = j;
         rv = sendmsg(sock, &msg, 0);
         for (int k = i-1, l = j-1; l >= 0; k--, l--)
            (*env)->ReleaseByteArrayElements(env, b[k], iov[l].iov_base, 0);
         if (-1 == rv) { handleerrno(env); return -1; }
         break;
      }
      size_t l = (*env)->GetArrayLength(env, b[i]);
      if (s+l > sblen || j == IOV_MAX) {
         msg.msg_iovlen = j;
         rv = sendmsg(sock, &msg, 0);
         j = 0;
         s = 0;
         for (int k = i-1, l = j-1; l >= 0; k--, l--)
            (*env)->ReleaseByteArrayElements(env, b[k], iov[l].iov_base, 0);
         if (-1 == rv) { handleerrno(env); return -1; }
      }
      iov[j].iov_base = (*env)->GetByteArrayElements(env, b[i], NULL);
      iov[j].iov_len = l;
      s += l;
   }

   free(iov);
   free(b);
   return rv;
}

/*
 * Class:     cx_ath_matthew_unix_UnixSocket
 * Method:    native_send_creds
 * Signature: (B)V
 */
JNIEXPORT void JNICALL Java_cx_ath_matthew_unix_UnixSocket_native_1send_1creds
  (JNIEnv * env, jobject o, jint sock, jbyte data)
{
   struct msghdr msg;
   struct iovec iov;
   msg.msg_name = NULL;
   msg.msg_namelen = 0;
   msg.msg_flags = 0;
   msg.msg_iov = &iov;
   msg.msg_iovlen = 1;
   msg.msg_control = NULL;
   msg.msg_controllen = 0;
   iov.iov_base = &data;
   iov.iov_len = 1;

#ifdef SCM_CREDENTIALS
   char buf[CMSG_SPACE(sizeof(struct ucred))];
   msg.msg_control = buf;
   msg.msg_controllen = sizeof buf;
   struct cmsghdr *cmsg;
   struct ucred *creds;

   cmsg = CMSG_FIRSTHDR(&msg);
   cmsg->cmsg_level = SOL_SOCKET;
   cmsg->cmsg_type = SCM_CREDENTIALS;
   cmsg->cmsg_len = CMSG_LEN(sizeof(struct ucred));
   /* Initialize the payload: */
   creds = (struct ucred *)CMSG_DATA(cmsg);
   creds->pid = getpid();
   creds->uid = getuid();
   creds->gid = getgid();
#endif

   int rv = sendmsg(sock, &msg, 0);
   if (-1 == rv) { handleerrno(env); }
}

#ifdef __cplusplus
}
#endif
