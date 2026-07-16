# Examples

Standalone C++ examples demonstrating the SignalWire AI Agents SDK. Each file
has a `main()` and can be compiled independently against the SDK library.

## Building an Example

```bash
# From the project root (after building the library):
g++ -std=c++20 -I include -I deps examples/simple_agent.cpp \
    -L build -lsignalwire -lssl -lcrypto -lpthread -o simple_agent
./simple_agent
```

## Agent Examples

| File | Description |
|------|-------------|
| [simple_agent.cpp](simple_agent.cpp) | POM prompts, tools, hints, languages, SIP routing |
| [simple_dynamic_agent.cpp](simple_dynamic_agent.cpp) | Per-request customization via DynamicConfigCallback |
| [simple_dynamic_enhanced.cpp](simple_dynamic_enhanced.cpp) | Enhanced dynamic config: VIP, department, customer ID, language |
| [simple_static_agent.cpp](simple_static_agent.cpp) | Static prompt-only agent (no tools) |
| [declarative_agent.cpp](declarative_agent.cpp) | Agent built from JSON-like config |
| [multi_agent_server.cpp](multi_agent_server.cpp) | Multiple agents on a single port via AgentServer |
| [custom_path_agent.cpp](custom_path_agent.cpp) | Agent at a custom HTTP route |
| [kubernetes_ready_agent.cpp](kubernetes_ready_agent.cpp) | Production-ready with health checks and signal handling |
| [lambda_agent.cpp](lambda_agent.cpp) | Serverless Lambda handler with global agent for cold-start reuse |
| [comprehensive_dynamic_agent.cpp](comprehensive_dynamic_agent.cpp) | Full dynamic config: tenant, voice, model, instructions |

## Tool & Action Examples

| File | Description |
|------|-------------|
| [swaig_features_agent.cpp](swaig_features_agent.cpp) | All FunctionResult action types: state, media, speech, SMS |
| [call_flow_and_actions_demo.cpp](call_flow_and_actions_demo.cpp) | 5-phase verb pipeline: pre-answer, answer, post-answer, post-AI |
| [record_call_example.cpp](record_call_example.cpp) | Start/stop call recording |
| [tap_example.cpp](tap_example.cpp) | Stream call audio to external RTP endpoint |
| [room_and_sip_example.cpp](room_and_sip_example.cpp) | Video rooms, SIP REFER, conferences |
| [session_and_state_demo.cpp](session_and_state_demo.cpp) | Global data, session tokens, callbacks |

## DataMap Examples

| File | Description |
|------|-------------|
| [datamap_demo.cpp](datamap_demo.cpp) | Weather API, expression routing, knowledge search |
| [advanced_datamap_demo.cpp](advanced_datamap_demo.cpp) | Multi-webhook fallback, foreach, expression routing |

## Contexts & Steps Examples

| File | Description |
|------|-------------|
| [contexts_demo.cpp](contexts_demo.cpp) | Multi-persona workflow (sales, tech support, manager) |
| [gather_info_demo.cpp](gather_info_demo.cpp) | GatherInfo with question collection |
| [dynamic_info_gatherer_example.cpp](dynamic_info_gatherer_example.cpp) | Dynamic InfoGatherer with callback-based question selection |

## Skills Examples

| File | Description |
|------|-------------|
| [skills_demo.cpp](skills_demo.cpp) | datetime, math, web_search skills |
| [joke_agent.cpp](joke_agent.cpp) | Joke skill for entertainment |
| [joke_skill_demo.cpp](joke_skill_demo.cpp) | Joke skill via modular skills system with DataMap |
| [web_search_agent.cpp](web_search_agent.cpp) | Google web search skill |
| [web_search_multi_instance_demo.cpp](web_search_multi_instance_demo.cpp) | Multiple web search instances (general, news, quick) |
| [wikipedia_demo.cpp](wikipedia_demo.cpp) | Wikipedia search skill |
| [datasphere.cpp](datasphere.cpp) | SignalWire Datasphere knowledge search |
| [datasphere_multi_instance_demo.cpp](datasphere_multi_instance_demo.cpp) | Multiple DataSphere instances with custom tool names |
| [datasphere_serverless_env.cpp](datasphere_serverless_env.cpp) | DataSphere serverless from environment variables |
| [datasphere_webhook_env_demo.cpp](datasphere_webhook_env_demo.cpp) | Webhook-based DataSphere from environment variables |
| [mcp_gateway_demo.cpp](mcp_gateway_demo.cpp) | MCP gateway tool integration |

## Prefab Examples

| File | Description |
|------|-------------|
| [info_gatherer_example.cpp](info_gatherer_example.cpp) | Sequential question collection |
| [survey_agent_example.cpp](survey_agent_example.cpp) | Typed survey with validation |
| [receptionist_agent_example.cpp](receptionist_agent_example.cpp) | Department routing with call transfer |
| [faq_bot_agent.cpp](faq_bot_agent.cpp) | Keyword-based FAQ matching |
| [concierge_agent_example.cpp](concierge_agent_example.cpp) | Venue information assistant |

## SWML Service Examples

| File | Description |
|------|-------------|
| [auto_vivified_example.cpp](auto_vivified_example.cpp) | Auto-vivified verb methods on SWMLService |
| [swml_service_example.cpp](swml_service_example.cpp) | Low-level SWML document builder |
| [swml_service_routing_example.cpp](swml_service_routing_example.cpp) | Multi-section routing with goto/label |
| [dynamic_swml_service.cpp](dynamic_swml_service.cpp) | Dynamic SWML per request |

## LLM Configuration

| File | Description |
|------|-------------|
| [llm_params_demo.cpp](llm_params_demo.cpp) | Temperature, top_p, barge_confidence tuning |
| [multi_endpoint_agent.cpp](multi_endpoint_agent.cpp) | Webhook URLs, query params, function includes |

## Client Examples

| File | Description |
|------|-------------|
| [relay_demo.cpp](relay_demo.cpp) | RELAY client: answer calls, play TTS (stub transport) |
| [rest_demo.cpp](rest_demo.cpp) | REST client: manage resources, place calls |

## RELAY Examples (relay/examples/)

| File | Description |
|------|-------------|
| [relay_answer_and_welcome.cpp](../relay/examples/relay_answer_and_welcome.cpp) | Answer inbound call, play greeting |
| [relay_dial_and_play.cpp](../relay/examples/relay_dial_and_play.cpp) | Dial outbound, play TTS |
| [relay_ivr_connect.cpp](../relay/examples/relay_ivr_connect.cpp) | IVR with DTMF, connect |

## REST Examples (rest/examples/)

| File | Description |
|------|-------------|
| [rest_manage_resources.cpp](../rest/examples/rest_manage_resources.cpp) | Create agent, search numbers, place call |
| [rest_datasphere_search.cpp](../rest/examples/rest_datasphere_search.cpp) | Upload document, semantic search |
| [rest_calling_play_and_record.cpp](../rest/examples/rest_calling_play_and_record.cpp) | Call with play and record |
| [rest_calling_ivr_and_ai.cpp](../rest/examples/rest_calling_ivr_and_ai.cpp) | IVR collect and detect |
| [rest_fabric_subscribers_and_sip.cpp](../rest/examples/rest_fabric_subscribers_and_sip.cpp) | Subscribers, SIP endpoints |
| [rest_fabric_swml_and_callflows.cpp](../rest/examples/rest_fabric_swml_and_callflows.cpp) | SWML scripts, call flows |
| [rest_fabric_conference_rooms.cpp](../rest/examples/rest_fabric_conference_rooms.cpp) | Conferences, routing rules |
| [rest_phone_number_management.cpp](../rest/examples/rest_phone_number_management.cpp) | Search and manage numbers |
| [rest_video_rooms.cpp](../rest/examples/rest_video_rooms.cpp) | Video rooms, sessions, recordings |
| [rest_queues_mfa_and_recordings.cpp](../rest/examples/rest_queues_mfa_and_recordings.cpp) | Queues, MFA, recordings |
| [rest_10dlc_registration.cpp](../rest/examples/rest_10dlc_registration.cpp) | 10DLC registration workflow |
