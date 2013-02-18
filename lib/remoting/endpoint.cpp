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

#include "i2-remoting.h"

using namespace icinga;

static AttributeDescription endpointAttributes[] = {
	{ "node", Attribute_Replicated },
	{ "service", Attribute_Replicated },
	{ "subscriptions", Attribute_Replicated },
	{ "client", Attribute_Transient }
};

REGISTER_TYPE(Endpoint, endpointAttributes);

signals2::signal<void (const Endpoint::Ptr&)> Endpoint::OnConnected;
signals2::signal<void (const Endpoint::Ptr&)> Endpoint::OnDisconnected;
signals2::signal<void (const Endpoint::Ptr&, const String& topic)> Endpoint::OnSubscriptionRegistered;
signals2::signal<void (const Endpoint::Ptr&, const String& topic)> Endpoint::OnSubscriptionUnregistered;

/**
 * Constructor for the Endpoint class.
 *
 * @param properties A serialized dictionary containing attributes.
 */
Endpoint::Endpoint(const Dictionary::Ptr& serializedUpdate)
	: DynamicObject(serializedUpdate)
{ }

/**
 * Checks whether an endpoint with the specified name exists.
 *
 * @param name The name of the endpoint.
 * @returns true if the endpoint exists, false otherwise.
 */
bool Endpoint::Exists(const String& name)
{
	return (DynamicObject::GetObject("Endpoint", name));
}

/**
 * Retrieves an endpoint by name.
 *
 * @param name The name of the endpoint.
 * @returns The endpoint.
 */
Endpoint::Ptr Endpoint::GetByName(const String& name)
{
        DynamicObject::Ptr configObject = DynamicObject::GetObject("Endpoint", name);

        if (!configObject)
                BOOST_THROW_EXCEPTION(invalid_argument("Endpoint '" + name + "' does not exist."));

        return dynamic_pointer_cast<Endpoint>(configObject);
}

/**
 * Helper function for creating new endpoint objects.
 *
 * @param name The name of the new endpoint.
 * @param replicated Whether replication is enabled for the endpoint object.
 * @param local Whether the new endpoint should be local.
 * @returns The new endpoint.
 */
Endpoint::Ptr Endpoint::MakeEndpoint(const String& name, bool replicated, bool local)
{
	ConfigItemBuilder::Ptr endpointConfig = boost::make_shared<ConfigItemBuilder>();
	endpointConfig->SetType("Endpoint");
	endpointConfig->SetName((!replicated && local) ? "local:" + name : name);
	endpointConfig->SetLocal(!replicated);
	endpointConfig->AddExpression("local", OperatorSet, local);

	DynamicObject::Ptr object = endpointConfig->Compile()->Commit();
	return dynamic_pointer_cast<Endpoint>(object);
}

/**
 * Checks whether this is a local endpoint.
 *
 * @returns true if this is a local endpoint, false otherwise.
 */
bool Endpoint::IsLocalEndpoint(void) const
{
	Value value = Get("local");

	return (!value.IsEmpty() && value);
}

/**
 * Checks whether this endpoint is connected.
 *
 * @returns true if the endpoint is connected, false otherwise.
 */
bool Endpoint::IsConnected(void) const
{
	if (IsLocalEndpoint()) {
		return true;
	} else {
		JsonRpcConnection::Ptr client = GetClient();

		return (client && client->GetStream()->IsConnected());
	}
}

JsonRpcConnection::Ptr Endpoint::GetClient(void) const
{
	return Get("client");
}

void Endpoint::SetClient(const JsonRpcConnection::Ptr& client)
{
	Set("client", client);
	client->OnNewMessage.connect(boost::bind(&Endpoint::NewMessageHandler, this, _2));
	client->OnClosed.connect(boost::bind(&Endpoint::ClientClosedHandler, this));

	OnConnected(GetSelf());
}

/**
 * Registers a topic subscription for this endpoint.
 *
 * @param topic The name of the topic.
 */
void Endpoint::RegisterSubscription(const String& topic)
{
	Dictionary::Ptr subscriptions = GetSubscriptions();

	if (!subscriptions)
		subscriptions = boost::make_shared<Dictionary>();

	if (!subscriptions->Contains(topic)) {
		Dictionary::Ptr newSubscriptions = subscriptions->ShallowClone();
		newSubscriptions->Set(topic, topic);
		SetSubscriptions(newSubscriptions);
	}
}

/**
 * Removes a topic subscription from this endpoint.
 *
 * @param topic The name of the topic.
 */
void Endpoint::UnregisterSubscription(const String& topic)
{
	Dictionary::Ptr subscriptions = GetSubscriptions();

	if (subscriptions && subscriptions->Contains(topic)) {
		Dictionary::Ptr newSubscriptions = subscriptions->ShallowClone();
		newSubscriptions->Remove(topic);
		SetSubscriptions(newSubscriptions);
	}
}

/**
 * Checks whether the endpoint has a subscription for the specified topic.
 *
 * @param topic The name of the topic.
 * @returns true if the endpoint is subscribed to the topic, false otherwise.
 */
bool Endpoint::HasSubscription(const String& topic) const
{
	Dictionary::Ptr subscriptions = GetSubscriptions();

	return (subscriptions && subscriptions->Contains(topic));
}

/**
 * Removes all subscriptions for the endpoint.
 */
void Endpoint::ClearSubscriptions(void)
{
	Set("subscriptions", Empty);
}

Dictionary::Ptr Endpoint::GetSubscriptions(void) const
{
	return Get("subscriptions");
}

void Endpoint::SetSubscriptions(const Dictionary::Ptr& subscriptions)
{
	Set("subscriptions", subscriptions);
}

void Endpoint::RegisterTopicHandler(const String& topic, const function<Endpoint::Callback>& callback)
{
	map<String, shared_ptr<signals2::signal<Endpoint::Callback> > >::iterator it;
	it = m_TopicHandlers.find(topic);

	shared_ptr<signals2::signal<Endpoint::Callback> > sig;

	if (it == m_TopicHandlers.end()) {
		sig = boost::make_shared<signals2::signal<Endpoint::Callback> >();
		m_TopicHandlers.insert(make_pair(topic, sig));
	} else {
		sig = it->second;
	}

	sig->connect(callback);

	RegisterSubscription(topic);
}

void Endpoint::UnregisterTopicHandler(const String& topic, const function<Endpoint::Callback>& callback)
{
	// TODO: implement
	//m_TopicHandlers[method] -= callback;
	//UnregisterSubscription(method);

	BOOST_THROW_EXCEPTION(NotImplementedException());
}

void Endpoint::OnAttributeChanged(const String& name, const Value& oldValue)
{
	if (name == "subscriptions") {
		Dictionary::Ptr oldSubscriptions, newSubscriptions;

		if (oldValue.IsObjectType<Dictionary>())
			oldSubscriptions = oldValue;

		newSubscriptions = GetSubscriptions();

		if (oldSubscriptions) {
			String subscription;
			BOOST_FOREACH(tie(tuples::ignore, subscription), oldSubscriptions) {
				if (!newSubscriptions || !newSubscriptions->Contains(subscription)) {
					Logger::Write(LogInformation, "remoting", "Removed subscription for '" + GetName() + "': " + subscription);
					OnSubscriptionUnregistered(GetSelf(), subscription);
				}
			}
		}

		if (newSubscriptions) {
			String subscription;
			BOOST_FOREACH(tie(tuples::ignore, subscription), newSubscriptions) {
				if (!oldSubscriptions || !oldSubscriptions->Contains(subscription)) {
					Logger::Write(LogDebug, "remoting", "New subscription for '" + GetName() + "': " + subscription);
					OnSubscriptionRegistered(GetSelf(), subscription);
				}
			}
		}
	}
}

void Endpoint::ProcessRequest(const Endpoint::Ptr& sender, const RequestMessage& request)
{
	if (!IsConnected()) {
		// TODO: persist the message
		return;
	}

	if (IsLocalEndpoint()) {
		String method;
		if (!request.GetMethod(&method))
			return;

		map<String, shared_ptr<signals2::signal<Endpoint::Callback> > >::iterator it;
		it = m_TopicHandlers.find(method);

		if (it == m_TopicHandlers.end())
			return;

		Application::GetEQ().Post(boost::bind(boost::ref(*it->second), GetSelf(), sender, request));
	} else {
		GetClient()->SendMessage(request);
	}
}

void Endpoint::ProcessResponse(const Endpoint::Ptr& sender, const ResponseMessage& response)
{
	if (!IsConnected())
		return;

	if (IsLocalEndpoint())
		EndpointManager::GetInstance()->ProcessResponseMessage(sender, response);
	else
		GetClient()->SendMessage(response);
}

void Endpoint::NewMessageHandler(const MessagePart& message)
{
	Endpoint::Ptr sender = GetSelf();

	if (ResponseMessage::IsResponseMessage(message)) {
		/* rather than routing the message to the right virtual
		 * endpoint we just process it here right away. */
		EndpointManager::GetInstance()->ProcessResponseMessage(sender, message);
		return;
	}

	RequestMessage request = message;

	String method;
	if (!request.GetMethod(&method))
		return;

	String id;
	if (request.GetID(&id))
		EndpointManager::GetInstance()->SendAnycastMessage(sender, request);
	else
		EndpointManager::GetInstance()->SendMulticastMessage(sender, request);
}

void Endpoint::ClientClosedHandler(void)
{
	/*try {
		GetClient()->CheckException();
	} catch (const exception& ex) {
		stringstream message;
		message << "Error occured for JSON-RPC socket: Message=" << diagnostic_information(ex);

		Logger::Write(LogWarning, "jsonrpc", message.str());
	}*/

	Logger::Write(LogWarning, "jsonrpc", "Lost connection to endpoint: identity=" + GetName());

	// TODO: _only_ clear non-persistent subscriptions
	// unregister ourselves if no persistent subscriptions are left (use a
	// timer for that, once we have a TTL property for the topics)
	ClearSubscriptions();

	Set("client", Empty);

	OnDisconnected(GetSelf());
}

/**
 * Gets the node address for this endpoint.
 *
 * @returns The node address (hostname).
 */
String Endpoint::GetNode(void) const
{
	return Get("node");
}

/**
 * Gets the service name for this endpoint.
 *
 * @returns The service name (port).
 */
String Endpoint::GetService(void) const
{
	return Get("service");
}
