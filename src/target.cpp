/*_############################################################################
  _## 
  _##  target.cpp  
  _##
  _##  SNMP++ v3.4
  _##  -----------------------------------------------
  _##  Copyright (c) 2001-2025 Jochen Katz, Frank Fock
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

  T A R G E T . C P P

  DESIGN + AUTHOR:  Peter E Mellquist

  DESCRIPTION:      Target class defines target SNMP agents.

=====================================================================*/
char target_cpp_version[]="#(@) SNMP++ $Id$";

#include <libsnmp.h>

#include "snmp_pp/target.h"
#include "snmp_pp/v3.h"

#ifdef SNMP_PP_NAMESPACE
namespace Snmp_pp {
#endif

#define PUBLIC "public"

#ifdef _SNMPv3
#define UTARGET_DEFAULT_VERSION version3
#else
#define UTARGET_DEFAULT_VERSION version1
#endif

// class variables for default behavior control
unsigned long SnmpTarget::default_timeout = 100;
int SnmpTarget::default_retries = 1;

//----------------------------------------------------------------------
//--------[ Abstract SnmpTarget Member Functions ]----------------------
//----------------------------------------------------------------------

// get the address
bool SnmpTarget::get_address(GenAddress &address) const
{
  if (!validity) return false;

  address = my_address;
  return true;
}

// set the address
bool SnmpTarget::set_address(const Address &address)
{
   my_address = address;
   if (my_address.valid())
      validity = true;
   else
      validity = false;

   return validity;
}

SnmpTarget* SnmpTarget::clone() const
{
  GenAddress addr = my_address;
  SnmpTarget* res = new SnmpTarget;
  res->set_timeout(timeout);
  res->set_retry(retries);
  res->set_address(addr);
  res->set_version(version);
  return res;
}

//=============[ int operator == SnmpTarget, SnmpTarget ]======================
// equivlence operator overloaded
int SnmpTarget::operator==( const SnmpTarget &rhs) const
{
  if (my_address != rhs.my_address) return 0;
  if (version    != rhs.version)    return 0;
  if (timeout    != rhs.timeout)    return 0;
  if (retries    != rhs.retries)    return 0;
  return 1;  // they are equal
}

// reset the object
void SnmpTarget::clear()
{
  validity = false;
  timeout  = default_timeout;
  retries  = default_retries;
  version  = version1;
  ttype    = type_base;
  my_address.clear();
}

// Get a human readable string for the given SNMP version.
const char* SnmpTarget::version_to_string(const snmp_version version)
{
  switch (version) {
    case version1:  return "SNMPv1";
    case version2c: return "SNMPv2c";
#ifdef _SNMPv3
    case version3: return "SNMPv3";
#endif
    default: return "unknown";
  }

}

//----------------------------------------------------------------------
//--------[ CTarget Member Functions ]----------------------------------
//----------------------------------------------------------------------

//---------[ CTarget::CTarget( void) ]----------------------------------
// CTarget constructor no args
CTarget::CTarget()
  : read_community(PUBLIC), write_community(PUBLIC)
{
  ttype = type_ctarget; // overwrite value set in SnmpTarget()
}

//-----------[ CTarget:: CTarget ]-------------------------------------
// CTarget constructor with args
CTarget::CTarget(const Address &address,
                 const char *read_community_name,
                 const char *write_community_name)
  : SnmpTarget(address),
    read_community(read_community_name), write_community(write_community_name)
{
  ttype = type_ctarget; // overwrite value set in SnmpTarget()
}

//-----------[ CTarget:: CTarget ]-----------------------------------
// CTarget constructor with args
CTarget::CTarget(const Address &address,
		 const OctetStr& read_community_name,
		 const OctetStr& write_community_name)
  : SnmpTarget(address),
    read_community(read_community_name), write_community(write_community_name)
{
  ttype = type_ctarget; // overwrite value set in SnmpTarget()
}


//-----------[ CTarget:: CTarget( Address &address) ]--------------
// CTarget constructor with args
CTarget::CTarget(const Address &address)
  : SnmpTarget(address), read_community(PUBLIC), write_community(PUBLIC)
{
  ttype = type_ctarget; // overwrite value set in SnmpTarget()
}

//-----------[ CTarget:: CTarget( const CTarget &target) ]-------
// CTarget constructor with args
CTarget::CTarget(const CTarget &target)
  : SnmpTarget(),
    read_community(target.read_community),
    write_community(target.write_community)
{
   my_address = target.my_address;
   timeout    = target.timeout;
   retries    = target.retries;
   version    = target.version;
   validity   = target.validity;
   ttype = type_ctarget; // overwrite value set in SnmpTarget()
}

//----------[ CTarget::resolve_to_V1 ]---------------------------------
// resolve entity
// common interface for Community based targets
bool CTarget::resolve_to_C(OctetStr &read_comm, OctetStr &write_comm,
                           GenAddress &address, unsigned long &t,
                           int &r, unsigned char &v) const
{
   // if the target is invalid then return false
   if (!validity)
     return false;

   read_comm = read_community;
   write_comm = write_community;
   address = my_address;

   t = timeout;
   r = retries;
   v = version;

   return true;
}

// overloaded assignment
CTarget& CTarget::operator=(const CTarget& target)
{
  if (this == &target) return *this;  // check for self assignment

  timeout         = target.timeout;
  retries         = target.retries;
  read_community  = target.read_community;
  write_community = target.write_community;
  validity        = target.validity;
  my_address      = target.my_address;
  version         = target.version;
  return *this;
}

//=============[ int operator == CTarget, CTarget ]==========================
// equivlence operator overloaded
int CTarget::operator==(const CTarget &rhs) const
{
  if (SnmpTarget::operator==(rhs) == 0)       return 0;
  // need to compare all the members of a CTarget
  if (read_community  != rhs.read_community)  return 0;
  if (write_community != rhs.write_community) return 0;
  return 1; // equal
}

// reset the object
void CTarget::clear()
{
  SnmpTarget::clear();
  read_community.clear();
  write_community.clear();
  ttype = type_ctarget; // overwrite value set in SnmpTarget::clear()
}

//----------------------------------------------------------------------
//--------[ UTarget Member Functions ]----------------------------------
//----------------------------------------------------------------------

//---------[ UTarget::UTarget( void) ]----------------------------------
// UTarget constructor no args
UTarget::UTarget()
  : security_name(INITIAL_USER),
#ifdef _SNMPv3
  security_model(SNMP_SECURITY_MODEL_USM), engine_id(""), coexistence_security_name(nullptr)
#else
  security_model(SNMP_SECURITY_MODEL_V1)
#endif
{
  version = UTARGET_DEFAULT_VERSION;
  ttype = type_utarget;
}

//-----------[ UTarget:: UTarget ]-------------------------------------
// UTarget constructor with args
UTarget::UTarget(const Address &address, const char *sec_name, const int sec_model)
  : SnmpTarget(address), security_name(sec_name), security_model(sec_model)
#ifdef _SNMPv3
    ,engine_id(""), coexistence_security_name(nullptr)
#endif
{
  version = UTARGET_DEFAULT_VERSION;
  ttype = type_utarget;
}

//-----------[ UTarget:: UTarget ]-----------------------------------
// UTarget constructor with args
UTarget::UTarget(const Address &address, const OctetStr &sec_name, const int sec_model)
  : SnmpTarget(address), security_name(sec_name), security_model(sec_model)
#ifdef _SNMPv3
    ,engine_id(""), coexistence_security_name(nullptr)
#endif
{
  version = UTARGET_DEFAULT_VERSION;
  ttype = type_utarget;
}

//-----------[ UTarget:: UTarget( Address &address) ]--------------
// UTarget constructor with args
UTarget::UTarget(const Address &address)
  : SnmpTarget(address), security_name(INITIAL_USER),
#ifdef _SNMPv3
    security_model(SNMP_SECURITY_MODEL_USM), engine_id(""), coexistence_security_name(nullptr)
#else
    security_model(SNMP_SECURITY_MODEL_V1)
#endif
{
  version = UTARGET_DEFAULT_VERSION;
  ttype = type_utarget;
}

//-----------[ UTarget:: UTarget( const UTarget &target) ]-------
// UTarget constructor with args
UTarget::UTarget(const UTarget &target)
  : SnmpTarget(),
    security_name(target.security_name),
    security_model(target.security_model)
#ifdef _SNMPv3
    , engine_id(target.engine_id), coexistence_security_name(nullptr)
#endif
{
    my_address = target.my_address;
    timeout = target.timeout;
    retries = target.retries;
    version = target.version;
    validity = target.validity;
#ifdef _SNMPv3
    if (target.coexistence_security_name) {
        coexistence_security_name = new OctetStr(*target.coexistence_security_name);
    }
#endif
    ttype = type_utarget;
}


// set the address
bool UTarget::set_address(const Address &address)
{
#ifdef _SNMPv3
   engine_id = ""; // delete engine_id
#endif
   return SnmpTarget::set_address(address);
}

bool UTarget::resolve_to_U(OctetStr& sec_name, int &sec_model,
                          GenAddress &address, unsigned long &t,
                          int &r, unsigned char &v) const
{
  // if the target is invalid then return false
  if (!validity)
    return false;

  sec_name = security_name;
  sec_model = security_model;
  address = my_address;

  t = timeout;
  r = retries;
  v = version;

  return true;
}

// overloaded assignment
UTarget& UTarget::operator=(const UTarget& target)
{
  if (this == &target) return *this;  // check for self assignment

  timeout = target.timeout;
  retries = target.retries;

#ifdef _SNMPv3
  engine_id = target.engine_id;
  if (target.coexistence_security_name)
  {
    coexistence_security_name = new OctetStr(*target.coexistence_security_name);
  }
#endif
  security_name = target.security_name;
  security_model = target.security_model;
  version = target.version;

  validity = target.validity;
  my_address = target.my_address;

  return *this;
}

//=============[ int operator == UTarget, UTarget ]==========================
// equivlence operator overloaded
int UTarget::operator==(const UTarget &rhs) const
{
  if (SnmpTarget::operator==(rhs) == 0)     return 0;
  // need to compare all the members of a UTarget
  // but don`t compare engine_id
  if (security_name  != rhs.security_name)  return 0;
  if (security_model != rhs.security_model) return 0;
  return 1;  // they are equal
}

// reset the object
void UTarget::clear()
{
  SnmpTarget::clear();
  security_name = INITIAL_USER;
#ifdef _SNMPv3
  security_model = SNMP_SECURITY_MODEL_USM;
  engine_id.clear();
  if (coexistence_security_name)
  {
    delete coexistence_security_name;
    coexistence_security_name = nullptr;
  }
#else
  security_model = SNMP_SECURITY_MODEL_V1;
#endif
  version = UTARGET_DEFAULT_VERSION;
  ttype = type_utarget;
}

#ifdef SNMP_PP_NAMESPACE
} // end of namespace Snmp_pp
#endif 
