/******************************************************************************
 * Icinga 2                                                                   *
 * Copyright (C) 2012 Icinga Development Team (http://www.icinga.org/)        *
 *                                                                            *
 * This program is free software; you can redistribute it and/or              *
 * modify it under the terms of the GNU General Public License                *
 * as published by the Free Software Foundation; either version 2             *
 * of the License, or (at your option) any later version.                     *
 *                                                                            *
 * This program is distributed in the hope that it will be useful,            *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of             *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the              *
 * GNU General Public License for more details.                               *
 *                                                                            *
 * You should have received a copy of the GNU General Public License          *
 * along with this program; if not, write to the Free Software Foundation     *
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.             *
 ******************************************************************************/

#include "i2-icinga.h"

using namespace icinga;

static AttributeDescription serviceAttributes[] = {
	{ "scheduling_offset", Attribute_Transient },
	{ "first_check", Attribute_Transient },
	{ "next_check", Attribute_Replicated },
	{ "checker", Attribute_Replicated },
	{ "check_attempt", Attribute_Replicated },
	{ "state", Attribute_Replicated },
	{ "state_type", Attribute_Replicated },
	{ "last_result", Attribute_Replicated },
	{ "last_state_change", Attribute_Replicated },
	{ "last_hard_state_change", Attribute_Replicated },
	{ "enable_active_checks", Attribute_Replicated },
	{ "enable_passive_checks", Attribute_Replicated },
	{ "force_next_check", Attribute_Replicated },
	{ "acknowledgement", Attribute_Replicated },
	{ "acknowledgement_expiry", Attribute_Replicated },
	{ "downtimes", Attribute_Replicated },
	{ "comments", Attribute_Replicated },
	{ "last_notification", Attribute_Replicated },
	{ "next_notification", Attribute_Replicated }
};

REGISTER_TYPE(Service, serviceAttributes);

Service::Service(const Dictionary::Ptr& serializedObject)
	: DynamicObject(serializedObject)
{ }

Service::~Service(void)
{
	ServiceGroup::InvalidateMembersCache();
	Host::InvalidateServicesCache();
	Service::InvalidateDowntimesCache();
	Service::InvalidateCommentsCache();
}

String Service::GetDisplayName(void) const
{
	String value = Get("display_name");

	if (!value.IsEmpty())
		return value;

	return GetName();
}

/**
 * @threadsafety Always.
 */
bool Service::Exists(const String& name)
{
	return (DynamicObject::GetObject("Service", name));
}

/**
 * @threadsafety Always.
 */
Service::Ptr Service::GetByName(const String& name)
{
	DynamicObject::Ptr configObject = DynamicObject::GetObject("Service", name);

	if (!configObject)
		BOOST_THROW_EXCEPTION(invalid_argument("Service '" + name + "' does not exist."));

	return dynamic_pointer_cast<Service>(configObject);
}

/**
 * @threadsafety Always.
 */
Service::Ptr Service::GetByNamePair(const String& hostName, const String& serviceName)
{
	if (!hostName.IsEmpty()) {
		Host::Ptr host = Host::GetByName(hostName);
		ObjectLock olock(host);
		return host->GetServiceByShortName(serviceName);
	} else {
		return Service::GetByName(serviceName);
	}
}

Host::Ptr Service::GetHost(void) const
{
	String hostname = Get("host_name");

	if (hostname.IsEmpty())
		BOOST_THROW_EXCEPTION(runtime_error("Service object is missing the 'host_name' property."));

	return Host::GetByName(hostname);
}

Dictionary::Ptr Service::GetMacros(void) const
{
	return Get("macros");
}

Dictionary::Ptr Service::GetHostDependencies(void) const
{
	return Get("hostdependencies");
}

Dictionary::Ptr Service::GetServiceDependencies(void) const
{
	return Get("servicedependencies");
}

Dictionary::Ptr Service::GetGroups(void) const
{
	return Get("servicegroups");
}

String Service::GetShortName(void) const
{
	Value value = Get("short_name");

	if (value.IsEmpty())
		return GetName();

	return value;
}

bool Service::IsReachable(void) const
{
	BOOST_FOREACH(const Service::Ptr& service, GetParentServices()) {
		/* ignore pending services */
		if (!service->GetLastCheckResult())
			continue;

		/* ignore soft states */
		if (service->GetStateType() == StateTypeSoft)
			continue;

		/* ignore services states OK and Warning */
		if (service->GetState() == StateOK ||
		    service->GetState() == StateWarning)
			continue;

		return false;
	}

	BOOST_FOREACH(const Host::Ptr& host, GetParentHosts()) {
		/* ignore hosts that are up */
		if (host->IsUp())
			continue;

		return false;
	}

	return true;
}

AcknowledgementType Service::GetAcknowledgement(void)
{
	Value value = Get("acknowledgement");

	if (value.IsEmpty())
		return AcknowledgementNone;

	int ivalue = static_cast<int>(value);
	AcknowledgementType avalue = static_cast<AcknowledgementType>(ivalue);

	if (avalue != AcknowledgementNone) {
		double expiry = GetAcknowledgementExpiry();

		if (expiry != 0 && expiry < Utility::GetTime()) {
			avalue = AcknowledgementNone;
			SetAcknowledgement(avalue);
			SetAcknowledgementExpiry(0);
		}
	}

	return avalue;
}

void Service::SetAcknowledgement(AcknowledgementType acknowledgement)
{
	Set("acknowledgement", static_cast<long>(acknowledgement));
}

bool Service::IsAcknowledged(void)
{
	return GetAcknowledgement() != AcknowledgementNone;
}

double Service::GetAcknowledgementExpiry(void) const
{
	Value value = Get("acknowledgement_expiry");

	if (value.IsEmpty())
		return 0;

	return static_cast<double>(value);
}

void Service::SetAcknowledgementExpiry(double timestamp)
{
	Set("acknowledgement_expiry", timestamp);
}

void Service::OnAttributeChanged(const String& name, const Value& oldValue)
{
	if (name == "checker")
		OnCheckerChanged(GetSelf(), oldValue);
	else if (name == "next_check")
		OnNextCheckChanged(GetSelf(), oldValue);
	else if (name == "servicegroups")
		ServiceGroup::InvalidateMembersCache();
	else if (name == "host_name" || name == "short_name") {
		Host::InvalidateServicesCache();
		UpdateSlaveNotifications();
	} else if (name == "downtimes")
		Service::InvalidateDowntimesCache();
	else if (name == "comments")
		Service::InvalidateCommentsCache();
	else if (name == "notifications")
		UpdateSlaveNotifications();
	else if (name == "check_interval") {
		ConfigItem::Ptr item = ConfigItem::GetObject("Service", GetName());

		/* update the next check timestamp if we're the owner of this service */
		if (item && !IsAbstract())
			UpdateNextCheck();
	}
}

set<Host::Ptr> Service::GetParentHosts(void) const
{
	set<Host::Ptr> parents;

	/* The service's host is implicitly a parent. */
	parents.insert(GetHost());

	Dictionary::Ptr dependencies = GetHostDependencies();

	if (dependencies) {
		String key;
		BOOST_FOREACH(tie(key, tuples::ignore), dependencies) {
			parents.insert(Host::GetByName(key));
		}
	}

	return parents;
}

set<Service::Ptr> Service::GetParentServices(void) const
{
	set<Service::Ptr> parents;

	Dictionary::Ptr dependencies = GetServiceDependencies();

	if (dependencies) {
		String key;
		Value value;
		BOOST_FOREACH(tie(key, value), dependencies) {
			Service::Ptr service = GetHost()->GetServiceByShortName(value);

			if (service->GetName() == GetName())
				continue;

			parents.insert(service);
		}
	}

	return parents;
}

Dictionary::Ptr Service::CalculateDynamicMacros(void) const
{
	Dictionary::Ptr macros = boost::make_shared<Dictionary>();

	macros->Set("SERVICEDESC", GetShortName());
	macros->Set("SERVICEDISPLAYNAME", GetDisplayName());
	macros->Set("SERVICESTATE", StateToString(GetState()));
	macros->Set("SERVICESTATEID", GetState());
	macros->Set("SERVICESTATETYPE", StateTypeToString(GetStateType()));
	macros->Set("SERVICEATTEMPT", GetCurrentCheckAttempt());
	macros->Set("MAXSERVICEATTEMPT", GetMaxCheckAttempts());

	Dictionary::Ptr cr = GetLastCheckResult();

	if (cr) {
		macros->Set("SERVICEOUTPUT", cr->Get("output"));
		macros->Set("SERVICEPERFDATA", cr->Get("performance_data_raw"));
	} else {
		macros->Set("SERVICEOUTPUT", "");
		macros->Set("SERVICEPERFDATA", "");
	}

	return macros;
}
