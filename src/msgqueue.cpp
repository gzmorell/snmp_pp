/*_############################################################################
  _## 
  _##  msgqueue.cpp  
  _##
  _##  SNMP++ v3.4
  _##  -----------------------------------------------
  _##  Copyright (c) 2001-2021 Jochen Katz, Frank Fock
  _##
  _##  This software is based on SNMP++2.6 from Hewlett Packard:
  _##  
  _##    Copyright (c) 1996
  _##    Hewlett-Packard Company
  _##  
  _##  ATTENTION: USE OF THIS SOFTWARE IS SUBJECT TO THE FOLLOWING TERMS.
  _##  Permission to use, copy, modify, distribute and/or sell this software 
  _##  and/or its documentation is hereby granted without fee. User agrees 
  _##  to display the above copyright notice and this license notice in all 
  _##  copies of the software and any documentation of the software. User 
  _##  agrees to assume all liability for the use of the software; 
  _##  Hewlett-Packard, Frank Fock, and Jochen Katz make no representations 
  _##  about the suitability of this software for any purpose. It is provided 
  _##  "AS-IS" without warranty of any kind, either express or implied. User 
  _##  hereby grants a royalty-free license to any and all derivatives based
  _##  upon this software code base. 
  _##  
  _##########################################################################*/
/*===================================================================

  Copyright (c) 1999
  Hewlett-Packard Company

  ATTENTION: USE OF THIS SOFTWARE IS SUBJECT TO THE FOLLOWING TERMS.
  Permission to use, copy, modify, distribute and/or sell this software
  and/or its documentation is hereby granted without fee. User agrees
  to display the above copyright notice and this license notice in all
  copies of the software and any documentation of the software. User
  agrees to assume all liability for the use of the software; Hewlett-Packard
  makes no representations about the suitability of this software for any
  purpose. It is provided "AS-IS" without warranty of any kind,either express
  or implied. User hereby grants a royalty-free license to any and all
  derivatives based upon this software code base.

      M S G Q U E U E . C P P

      MSG QUEUE CLASS DECLARATION

      Author:           Peter E Mellquist

=====================================================================*/
char msgqueue_version[]="#(@) SNMP++ $Id$";

#include <libsnmp.h>

//-----[ includes ]----------------------------------------------------

//----[ snmp++ includes ]----------------------------------------------

#include "snmp_pp/msgqueue.h"        // queue for holding outstanding messages
#include "snmp_pp/snmpmsg.h"
#include "snmp_pp/eventlistholder.h"
#include "snmp_pp/log.h"
#include "snmp_pp/vb.h"
#include "snmp_pp/v3.h"

#ifdef SNMP_PP_NAMESPACE
namespace Snmp_pp {
#endif


static const char *loggerModuleName = "snmp++.msgqueue";

//--------[ externs ]---------------------------------------------------
extern int send_snmp_request(SnmpSocket sock, unsigned char *send_buf,
                             size_t send_len, const Address &address);
extern int receive_snmp_response(SnmpSocket sock, Snmp &snmp_session,
                                 Pdu &pdu, UdpAddress &fromaddress,
                                 OctetStr &engine_id, bool process_msg = true);

//----[ CSNMPMessage class ]-------------------------------------------

CSNMPMessage::CSNMPMessage(unsigned long id,
                           Snmp * snmp,
                           SnmpSocket socket,
                           const SnmpTarget &target,
                           Pdu &pdu,
                           unsigned char * rawPdu,
                           size_t rawPduLen,
                           const Address & address,
                           snmp_callback callBack,
                           void * callData):
  m_uniqueId(id), m_snmp(snmp), m_socket(socket), m_pdu(pdu),
  m_rawPduLen(rawPduLen), m_callBack(callBack), m_callData(callData),
  m_reason(0), m_received(0), m_locked(false)
{
  m_pdu.set_error_index(0);
  m_pdu.set_error_status(0);
  m_pdu.set_request_id(m_uniqueId);

  m_rawPdu = new unsigned char[rawPduLen];
  memcpy(m_rawPdu, rawPdu, rawPduLen);
  m_address = (Address *)address.clone();
  m_target = target.clone();

  SetSendTime();
}

CSNMPMessage::~CSNMPMessage()
{
  delete [] m_rawPdu;
  delete m_address;
  delete m_target;
}

void CSNMPMessage::Update(unsigned char *rawPdu, size_t rawPduLen)
{
  LOG_BEGIN(loggerModuleName, DEBUG_LOG | 10);
  LOG("MsgQueue: Update Entry (id)");
  LOG(m_pdu.get_request_id());
  LOG_END;

  if (rawPduLen != m_rawPduLen) {
    delete [] m_rawPdu;
    m_rawPdu = new unsigned char[rawPduLen];
  }
  memcpy(m_rawPdu, rawPdu, rawPduLen);
  m_rawPduLen = rawPduLen;
}

void CSNMPMessage::SetSendTime()
{
  m_sendTime.refresh();

  // Kludge: When this was first designed the units were millisecs
  // However, later on the units for the target class were changed
  // to hundreths of secs.  Multiply the hundreths of secs by 10
  // to create the millisecs which the rest of the objects use.
  // 11-Dec-95 TM
  m_sendTime += (m_target->get_timeout() * 10);
}

int CSNMPMessage::SetPdu(const int reason, const Pdu &pdu,
                         const UdpAddress & /* fromaddress */)
{
#ifdef _SNMPv3
  if ((m_target->get_version() == version3) &&
      (m_pdu.get_message_id() != pdu.get_message_id()))
  {
    LOG_BEGIN(loggerModuleName, INFO_LOG | 1);
    LOG("MsgQueue: Ignore response that does not match message id: (id1) (type2) (msgid1) (msgid2");
    LOG(m_pdu.get_request_id());
    LOG(pdu.get_request_id());
    LOG(m_pdu.get_message_id());
    LOG(pdu.get_message_id());
    LOG_END;
    return -1;
  }
#endif

  if (Pdu::match_type(m_pdu.get_type(), pdu.get_type()) == false)
  {
    LOG_BEGIN(loggerModuleName, INFO_LOG | 1);
    LOG("MsgQueue: Response pdu type does not match, pdu is ignored: (id) (type1) (type2)");
    LOG(m_uniqueId);
    LOG(m_pdu.get_type());
    LOG(pdu.get_type());
    LOG_END;

    return -1;
  }

  unsigned short orig_type = m_pdu.get_type();
  if (m_received)
  {
    LOG_BEGIN(loggerModuleName, WARNING_LOG | 1);
    LOG("MsgQueue: Message is already marked as received (id) (reason) (new reason)");
    LOG(m_uniqueId);
    LOG(reason);
    LOG(m_reason);
    LOG_END;

    // TODO: better check based on error codes
    if (reason || !m_reason)
    {
      LOG_BEGIN(loggerModuleName, WARNING_LOG | 1);
      LOG("MsgQueue: ignoring the second pdu");
      LOG_END;

      return 0;
    }
  }
  m_received = 1;
  m_pdu = pdu;
  m_reason = reason;

  LOG_BEGIN(loggerModuleName, DEBUG_LOG | 10);
  LOG("MsgQueue: Response received (req id) (status) (msg id)");
  LOG(pdu.get_request_id());
  LOG(reason);
#ifdef _SNMPv3
  LOG(pdu.get_message_id());
#endif
  LOG_END;

  if ((orig_type == sNMP_PDU_INFORM) &&
      (m_pdu.get_type() == sNMP_PDU_RESPONSE))
  {
    // remove the first two vbs of the pdu if sysUpTime and notify_id
    if (m_pdu.get_vb_count() < 2)
      return 0;

    const Vb &vb1 = m_pdu.get_vb(0);
    if (vb1.get_syntax() != sNMP_SYNTAX_TIMETICKS)   return 0;
    if (vb1.get_oid()    != SNMP_MSG_OID_SYSUPTIME)  return 0;

    const Vb &vb2 = m_pdu.get_vb(1);
    if (vb2.get_syntax() != sNMP_SYNTAX_OID)         return 0;
    if (vb2.get_oid()    != SNMP_MSG_OID_TRAPID)     return 0;

    TimeTicks timeticks;
    Oid oid;

    vb1.get_value(timeticks);
    m_pdu.set_notify_timestamp(timeticks);

    vb2.get_value(oid);
    m_pdu.set_notify_id(oid);

    m_pdu.delete_vb(1);
    m_pdu.delete_vb(0);
  }
  return 0;
}

int CSNMPMessage::ResendMessage()
{
  if (m_received)
  {
    // Don't bother to resend if we already have the response
    SetSendTime();
    return SNMP_CLASS_SUCCESS;
  }

  LOG_BEGIN(loggerModuleName, DEBUG_LOG | 10);
#ifdef _SNMPv3
  LOG("MsgQueue: Message (msg id) (req id) (info)");
  LOG(m_pdu.get_message_id());
#else
  LOG("MsgQueue: Message (req id) (info)");
#endif
  LOG(m_pdu.get_request_id());
  LOG((m_target->get_retry() <= 0) ? "TIMEOUT" : "RESEND");
  LOG_END;

  if (m_target->get_retry() <= 0)
  {
    // This message has timed out
    Callback(SNMP_CLASS_TIMEOUT);   // perform callback with the error
    return SNMP_CLASS_TIMEOUT;
  }

  m_target->set_retry(m_target->get_retry() - 1);
  SetSendTime();
  int status;
#ifdef _SNMPv3
  if (m_target->get_version() == version3) {
    // delete entry in cache
    if (m_snmp->get_mpv3())
	    m_snmp->get_mpv3()->delete_from_cache(m_pdu.get_request_id());
    if (m_callBack) {
      // Map the action back to the _ASYNC value, so snmp_engine will handle it correctly
      unsigned short action;
      m_snmp->map_action_to_async(m_pdu.get_type(), action);
      m_pdu.set_type(action);
    }
    status = m_snmp->snmp_engine(m_pdu, m_pdu.get_error_status(), m_pdu.get_error_index(),
		    *m_target, m_callBack, m_callData, m_socket, 0, this);
  }
  else
#endif
    status = send_snmp_request(m_socket, m_rawPdu, m_rawPduLen, *m_address);

  if (status != 0)
    return SNMP_CLASS_TL_FAILED;

  return SNMP_CLASS_SUCCESS;
}

int CSNMPMessage::Callback(const int reason)
{
  if (m_callBack)
  {
    // prevent callbacks from using this message
    snmp_callback tmp_callBack = m_callBack;
    m_callBack = NULL;

    tmp_callBack(reason, m_snmp, m_pdu, *m_target, m_callData);
    return 0;
  }
  return 1;
}

//----[ CSNMPMessageQueueElt class ]--------------------------------------

CSNMPMessageQueue::CSNMPMessageQueueElt::CSNMPMessageQueueElt(
                                           CSNMPMessage *message,
                                           CSNMPMessageQueueElt *next,
                                           CSNMPMessageQueueElt *previous):
  m_message(message), m_Next(next), m_previous(previous)
{
  /* Finish insertion into doubly linked list */
  if (m_Next)     m_Next->m_previous = this;
  if (m_previous) m_previous->m_Next = this;
}

CSNMPMessageQueue::CSNMPMessageQueueElt::~CSNMPMessageQueueElt()
{
  /* Do deletion form doubly linked list */
  if (m_Next)     m_Next->m_previous = m_previous;
  if (m_previous) m_previous->m_Next = m_Next;
  if (m_message)  delete m_message;
}

CSNMPMessage *CSNMPMessageQueue::CSNMPMessageQueueElt::TestId(const unsigned long uniqueId)
{
  if (m_message && (m_message->GetId() == uniqueId))
    return m_message;
  return 0;
}



//----[ CSNMPMessageQueue class ]--------------------------------------

CSNMPMessageQueue::CSNMPMessageQueue(EventListHolder *holder, Snmp *session)
  : m_head(0, 0, 0), m_msgCount(0), my_holder(holder), m_snmpSession(session)
{
}

CSNMPMessageQueue::~CSNMPMessageQueue()
{
  CSNMPMessageQueueElt *leftOver;
  lock();
    /*--------------------------------------------------------*/
    /* walk the list deleting any elements still on the queue */
    /*--------------------------------------------------------*/
  while ((leftOver = m_head.GetNext()))
  {
    if (leftOver->GetMessage()->IsLocked())
    {
      unlock();
      // TODO: should we sleep here
      lock();
    }
    else
      delete leftOver;
  }
  unlock();
}

CSNMPMessage * CSNMPMessageQueue::AddEntry(unsigned long id,
                                           Snmp * snmp,
                                           SnmpSocket socket,
                                           const SnmpTarget &target,
                                           Pdu &pdu,
                                           unsigned char * rawPdu,
                                           size_t rawPduLen,
                                           const Address & address,
                                           snmp_callback callBack,
                                           void * callData)
{
  if (snmp != m_snmpSession)
  {
    LOG_BEGIN(loggerModuleName, ERROR_LOG | 1);
    LOG("MsgQueue: Adding message for other Snmp object.");
    LOG_END;
  }

  CSNMPMessage *newMsg = new CSNMPMessage(id, snmp, socket, target, pdu,
                                          rawPdu, rawPduLen, address,
                                          callBack, callData);

  lock();
  // Insert entry at head of list, done automatically by the
  // constructor function, so don't use the return value.
  (void) new CSNMPMessageQueueElt(newMsg, m_head.GetNext(), &m_head);
  ++m_msgCount;
  int count = m_msgCount;
  unlock();
  
  LOG_BEGIN(loggerModuleName, DEBUG_LOG | 10);
  LOG("MsgQueue: Adding entry (req id) (count)");
  LOG(id);
  LOG(count);
  LOG_END;

  return newMsg;
}


CSNMPMessage *CSNMPMessageQueue::GetEntry(const unsigned long uniqueId)
{
  CSNMPMessageQueueElt *msgEltPtr = m_head.GetNext();

  while (msgEltPtr)
  {
    CSNMPMessage *returnVal = msgEltPtr->TestId(uniqueId);
    if (returnVal)
      return returnVal;
    msgEltPtr = msgEltPtr->GetNext();
  }
  return 0;
}

int CSNMPMessageQueue::DeleteEntry(const unsigned long uniqueId)
{
  bool loopAgain;
  do
  {
    loopAgain = false;
    CSNMPMessageQueueElt *msgEltPtr = m_head.GetNext();

    while (msgEltPtr)
    {
      if (msgEltPtr->TestId(uniqueId))
      {
        if (msgEltPtr->GetMessage())
        {
          if (msgEltPtr->GetMessage()->IsLocked())
          {
            unlock();
            // TODO: should we sleep here?
            lock();
            loopAgain = true;
          }
        }

        if (!loopAgain)
        {
          delete msgEltPtr;
          m_msgCount--;
          LOG_BEGIN(loggerModuleName, DEBUG_LOG | 10);
          LOG("MsgQueue: Removed entry (req id)");
          LOG(uniqueId);
          LOG_END;
          return SNMP_CLASS_SUCCESS;
        }
      }
      msgEltPtr = msgEltPtr->GetNext();
    }
  } while (loopAgain);
  return SNMP_CLASS_INVALID_REQID;
}

void CSNMPMessageQueue::DeleteSocketEntry(const SnmpSocket socket)
{
  lock();
  CSNMPMessageQueueElt *msgEltPtr = m_head.GetNext();
  while (msgEltPtr)
  {
    CSNMPMessage *msg = msgEltPtr->GetMessage();
    if (socket == msg->GetSocket())
    {
      if (msg->IsLocked())
      {
        unlock();
        // TODO: should we sleep here?
        lock();
        // start from beginning as list could have been changed
        msgEltPtr = m_head.GetNext();
      }
      else
      {
        // Make a callback with an error
        (void) msg->Callback(SNMP_CLASS_SESSION_DESTROYED);
        CSNMPMessageQueueElt *tmp_msgEltPtr = msgEltPtr;
        msgEltPtr = tmp_msgEltPtr->GetNext();
        // delete the entry
        delete tmp_msgEltPtr;
      }
    }
    else
      msgEltPtr = msgEltPtr->GetNext();
  }
  unlock();
}

CSNMPMessage * CSNMPMessageQueue::GetNextTimeoutEntry()
{
  CSNMPMessageQueueElt *msgEltPtr = m_head.GetNext();
  msec bestTime;
  msec sendTime(bestTime);
  CSNMPMessage *msg;
  CSNMPMessage *bestmsg = NULL;

  if (msgEltPtr) {
    bestmsg = msgEltPtr->GetMessage();
    bestmsg->GetSendTime(bestTime);
  }

  // This would be much simpler if the queue was an ordered list!
  while (msgEltPtr){
    msg = msgEltPtr->GetMessage();
    msg->GetSendTime(sendTime);
    if (bestTime  > sendTime) {
      bestTime = sendTime;
      bestmsg = msg;
    }
    msgEltPtr = msgEltPtr->GetNext();
  }
  return bestmsg;
}

int CSNMPMessageQueue::GetNextTimeout(msec &sendTime)
{
  CSNMPMessage *msg = GetNextTimeoutEntry();

  if (!msg)  return 1;    // nothing in the queue...

  msg->GetSendTime(sendTime);
  return 0;
}

#ifdef HAVE_POLL_SYSCALL

int CSNMPMessageQueue::GetFdCount()
{
  SnmpSynchronize _synchronize(*this); // instead of REENTRANT()

  CSNMPMessageQueueElt *msgEltPtr = m_head.GetNext();

  if (!msgEltPtr) return 0;

#ifndef SNMP_PP_IPv6
  // we have at least one message in the queue and Snmp class
  // can only have one socket, so
  return 1;
#else
  // we can have max 2 sockets
  SnmpSocket firstSocket = msgEltPtr->GetMessage()->GetSocket();

  msgEltPtr = msgEltPtr->GetNext();

  while (msgEltPtr)
  {
      if (msgEltPtr->GetMessage()->GetSocket() != firstSocket)
          return 2;
      msgEltPtr = msgEltPtr->GetNext();
  }
  return 1;
#endif
}

bool CSNMPMessageQueue::GetFdArray(struct pollfd *readfds, int &remaining)
{
  SnmpSynchronize _synchronize(*this); // instead of REENTRANT()

  CSNMPMessageQueueElt *msgEltPtr = m_head.GetNext();
  if (!msgEltPtr) return true;

  if (remaining <= 0) return false;

  SnmpSocket firstSocket = msgEltPtr->GetMessage()->GetSocket();
  readfds[0].fd = firstSocket;
  readfds[0].events = POLLIN;
  remaining--;

#ifndef SNMP_PP_IPv6
  // we have at least one message in the queue and Snmp class
  // can only have one socket, so we are done
  return true;
#else
  // we can have max 2 sockets
  msgEltPtr = msgEltPtr->GetNext();

  while (msgEltPtr)
  {
    if (msgEltPtr->GetMessage()->GetSocket() != firstSocket)
    {
      if (remaining <= 0) return false;
      readfds[1].fd = msgEltPtr->GetMessage()->GetSocket();
      readfds[1].events = POLLIN;
      remaining--;

      return true;
    }
    msgEltPtr = msgEltPtr->GetNext();
  }
  return true;
#endif
}

int CSNMPMessageQueue::HandleEvents(const struct pollfd *readfds,
                                    const int fds)
{
  for (int i=0; i < fds ; i++)
  {
    if (readfds[i].revents & POLLIN)
    {
      UdpAddress fromaddress;
      Pdu tmppdu;
      int status;
      int recv_status;
      OctetStr engine_id;

      tmppdu.set_request_id(0);

      // get the response and put it into a Pdu
      recv_status = receive_snmp_response(readfds[i].fd, *m_snmpSession,
                                          tmppdu, fromaddress, engine_id);

      unsigned long temp_req_id = tmppdu.get_request_id();
      if (!temp_req_id)
        continue;

      CSNMPMessage *msg = 0;
      bool redoGetEntry;
      do
      {
	redoGetEntry = false;
        lock();
        // find the corresponding msg in the message queue
        msg = GetEntry(temp_req_id);
        if (msg && msg->IsLocked())
        {
          unlock();
          // TODO: Should we sleep here?
          redoGetEntry = true;
        }
      } while (redoGetEntry);

      if (!msg)
      {
        unlock();
        LOG_BEGIN(loggerModuleName, INFO_LOG | 7);
        LOG("MsgQueue: Ignore received message without outstanding request (req id)");
        LOG(tmppdu.get_request_id());
        LOG_END;
        // the sent message is gone! probably was canceled, ignore it
        continue;
      }

      // save pdu back into the message
      status = msg->SetPdu(recv_status, tmppdu, fromaddress);

      if (status)
      {
        // received pdu does not match
        // @todo if version is SNMPv3 we must return a report
        //       unknown pdu handler!
        unlock();
        continue;
      }

#ifdef _SNMPv3
      if (engine_id.len() > 0)
      {
        SnmpTarget *target = msg->GetTarget();
        if ((target->get_type() == SnmpTarget::type_utarget) &&
            (target->get_version() == version3))
        {
          UdpAddress addr = target->get_address();

	    LOG_BEGIN(loggerModuleName, DEBUG_LOG | 14);
          LOG("MsgQueue: Adding engine id to table (addr) (id)");
          LOG(addr.get_printable());
          LOG(engine_id.get_printable());
          LOG_END;

          m_snmpSession->get_mpv3()->add_to_engine_id_table(engine_id,
                                          (char*)addr.IpAddress::get_printable(),
                                          addr.get_port());
        }
      }
#endif

      // Do the callback
      msg->SetLocked(true);
      unlock();
      status = msg->Callback(SNMP_CLASS_ASYNC_RESPONSE);
      lock();
      msg->SetLocked(false);

      if (!status)
      {
        // this is an asynch response and the callback is done.
        // no need to keep this message around;
        // Dequeue the message
        DeleteEntry(temp_req_id);
      }
      unlock();
    }
  }
  return SNMP_CLASS_SUCCESS;
}

#else // HAVE_POLL_SYSCALL

void CSNMPMessageQueue::GetFdSets(int &maxfds, fd_set &readfds,
                                  fd_set &, fd_set &)
{
  SnmpSynchronize _synchronize(*this); // REENTRANT
  CSNMPMessageQueueElt *msgEltPtr = m_head.GetNext();

  while (msgEltPtr)
  {
    SnmpSocket sock = msgEltPtr->GetMessage()->GetSocket();
    FD_SET(sock, &readfds);
    if (maxfds < SAFE_INT_CAST(sock+1))
      maxfds = SAFE_INT_CAST(sock+1);
    msgEltPtr = msgEltPtr->GetNext();
  }
}

int CSNMPMessageQueue::HandleEvents(const int maxfds,
                                    const fd_set &readfds,
                                    const fd_set &,
                                    const fd_set &)
{
  fd_set snmp_readfds, snmp_writefds, snmp_errfds;
  int tmp_maxfds = maxfds;

  // Only read from our own fds
  FD_ZERO(&snmp_readfds);
  FD_ZERO(&snmp_writefds);
  FD_ZERO(&snmp_errfds);
  GetFdSets(tmp_maxfds, snmp_readfds, snmp_writefds, snmp_errfds);

  for (int fd = 0; fd < maxfds; fd++)
  {
    if ((FD_ISSET(fd, &snmp_readfds)) &&
        (FD_ISSET(fd, (fd_set*)&readfds)))
    {
      UdpAddress fromaddress;
      Pdu tmppdu;
      int status;
      int recv_status;
      OctetStr engine_id;

      tmppdu.set_request_id(0);

      // get the response and put it into a Pdu
      recv_status = receive_snmp_response(fd, *m_snmpSession,
                                          tmppdu, fromaddress, engine_id);

      unsigned long temp_req_id = tmppdu.get_request_id();
      if (!temp_req_id)
        continue;

      CSNMPMessage *msg = 0;
      bool redoGetEntry;
      do
      {
        redoGetEntry = false;
        lock();
        // find the corresponding msg in the message queue
        msg = GetEntry(temp_req_id);
        if (msg && msg->IsLocked())
        {
          unlock();
          // TODO: Should we sleep here?
          redoGetEntry = true;
        }
      } while (redoGetEntry);

      if (!msg)
      {
        unlock();
        LOG_BEGIN(loggerModuleName, INFO_LOG | 7);
        LOG("MsgQueue: Ignore received message without outstanding request (req id)");
        LOG(tmppdu.get_request_id());
        LOG_END;
        // the sent message is gone! probably was canceled, ignore it
        continue;
      }

      // save pdu back into the message
      status = msg->SetPdu(recv_status, tmppdu, fromaddress);

      if (status)
      {
        // received pdu does not match
        // @todo if version is SNMPv3 we must return a report
        //       unknown pdu handler!
        unlock();
        continue;
      }

#ifdef _SNMPv3
      if (engine_id.len() > 0)
      {
        SnmpTarget *target = msg->GetTarget();
        if ((target->get_type() == SnmpTarget::type_utarget) &&
            (target->get_version() == version3))
        {
          if (tmppdu.get_type() == sNMP_PDU_REPORT || 
              tmppdu.get_type() == sNMP_PDU_RESPONSE) {
              
            UdpAddress addr = target->get_address();

            LOG_BEGIN(loggerModuleName, DEBUG_LOG | 14);
            LOG("MsgQueue: Adding engine id to table (addr) (id)");
            LOG(addr.get_printable());
            LOG(engine_id.get_printable());
            LOG_END;
            m_snmpSession->get_mpv3()->add_to_engine_id_table(engine_id,
                                         (char*)addr.IpAddress::get_printable(),
                                          addr.get_port());
          }
        }
      }
#endif

      // Do the callback
      msg->SetLocked(true);
      unlock();
      status = msg->Callback(SNMP_CLASS_ASYNC_RESPONSE);
      lock();
      msg->SetLocked(false);

      if (!status)
      {
        // this is an asynch response and the callback is done.
        // no need to keep this message around;
        // Dequeue the message
        DeleteEntry(temp_req_id);
      }
      unlock();
    } // if socket has data
  } // for all sockets
  return SNMP_CLASS_SUCCESS;
}

#endif // HAVE_POLL_SYSCALL

int CSNMPMessageQueue::DoRetries(const msec &now)
{
  CSNMPMessage *msg;
  msec sendTime(0, 0);
  int status = SNMP_CLASS_SUCCESS;
  lock();
  while ((msg = GetNextTimeoutEntry()))
  {
    msg->GetSendTime(sendTime);

    if (sendTime > now)
      break;  // the next timeout is still in the future...so we are done

    if (msg->IsLocked())
    {
      unlock();
      // TODO: do we really want to return without processing the rest?
      return status;
    }
    // send out the message again
    msg->SetLocked(true);
    unlock();
    status = msg->ResendMessage();
    lock();
    msg->SetLocked(false);
    if (status != 0)
    {
      if (status == SNMP_CLASS_TIMEOUT)
      {
        unsigned long req_id = msg->GetId();

        // Dequeue the message
        DeleteEntry(req_id);
#ifdef _SNMPv3
        // delete entry in cache
        if (m_snmpSession->get_mpv3())
          m_snmpSession->get_mpv3()->delete_from_cache(req_id);

        LOG_BEGIN(loggerModuleName, INFO_LOG | 6);
        LOG("MsgQueue: Message timed out, removed id from v3MP cache (rid)");
        LOG(req_id);
        LOG_END;
#endif
      }
      else
      {
        // Some other send error, should we dequeue the message?
        // do we really want to return without processing the rest?
        unlock();
        return status;
      }
    }
  }
  unlock();
  return status;
}

int CSNMPMessageQueue::Done(unsigned long id)
{
  SnmpSynchronize _synchronize(*this); // instead of REENTRANT()

  // FF: This is much more efficient than the above
  CSNMPMessage *msg = GetEntry(id);

  if (!msg) return 1; // the message is not in the queue...must have timed out

  if (msg->GetReceived())
      return 1;

  return 0;
}

#ifdef SNMP_PP_NAMESPACE
} // end of namespace Snmp_pp
#endif
