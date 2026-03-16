# Examples

Standalone C++ examples demonstrating the SignalWire AI Agents SDK. Each file
has a `main()` and can be compiled independently against the SDK library.

## Building an Example

```bash
# From the project root (after building the library):
g++ -std=c++17 -I include -I deps examples/simple_agent.cpp \
    -L build -lsignalwire_agents -lssl -lcrypto -lpthread -o simple_agent
./simple_agent
```

## Agent Examples

| File | Description |
|------|-------------|
| [simple_agent.cpp](simple_agent.cpp) | POM prompts, tools, hints, languages, SIP routing |
| [simple_dynamic_agent.cpp](simple_dynamic_agent.cpp) | Per-request customization via DynamicConfigCallback |
| [simple_static.cpp](simple_static.cpp) | Static prompt-only agent (no tools) |
| [declarative.cpp](declarative.cpp) | Agent built from JSON-like config |
| [multi_agent_server.cpp](multi_agent_server.cpp) | Multiple agents on a single port via AgentServer |
| [custom_path.cpp](custom_path.cpp) | Agent at a custom HTTP route |
| [kubernetes.cpp](kubernetes.cpp) | Production-ready with health checks and signal handling |
| [comprehensive_dynamic.cpp](comprehensive_dynamic.cpp) | Full dynamic config: tenant, voice, model, instructions |

## Tool & Action Examples

| File | Description |
|------|-------------|
| [swaig_features.cpp](swaig_features.cpp) | All FunctionResult action types: state, media, speech, SMS |
| [call_flow.cpp](call_flow.cpp) | 5-phase verb pipeline: pre-answer, answer, post-answer, post-AI |
| [record_call.cpp](record_call.cpp) | Start/stop call recording |
| [tap.cpp](tap.cpp) | Stream call audio to external RTP endpoint |
| [room_and_sip.cpp](room_and_sip.cpp) | Video rooms, SIP REFER, conferences |
| [session_state.cpp](session_state.cpp) | Global data, session tokens, callbacks |

## DataMap Examples

| File | Description |
|------|-------------|
| [datamap_demo.cpp](datamap_demo.cpp) | Weather API, expression routing, knowledge search |
| [advanced_datamap.cpp](advanced_datamap.cpp) | Multi-webhook fallback, foreach, expression routing |

## Contexts & Steps Examples

| File | Description |
|------|-------------|
| [contexts_demo.cpp](contexts_demo.cpp) | Multi-persona workflow (sales, tech support, manager) |
| [gather_info.cpp](gather_info.cpp) | GatherInfo with question collection |

## Skills Examples

| File | Description |
|------|-------------|
| [skills_demo.cpp](skills_demo.cpp) | datetime, math, web_search skills |
| [joke_agent.cpp](joke_agent.cpp) | Joke skill for entertainment |
| [web_search.cpp](web_search.cpp) | Google web search skill |
| [wikipedia.cpp](wikipedia.cpp) | Wikipedia search skill |
| [datasphere.cpp](datasphere.cpp) | SignalWire Datasphere knowledge search |
| [mcp_gateway.cpp](mcp_gateway.cpp) | MCP gateway tool integration |

## Prefab Examples

| File | Description |
|------|-------------|
| [prefab_info_gatherer.cpp](prefab_info_gatherer.cpp) | Sequential question collection |
| [prefab_survey.cpp](prefab_survey.cpp) | Typed survey with validation |
| [prefab_receptionist.cpp](prefab_receptionist.cpp) | Department routing with call transfer |
| [prefab_faq_bot.cpp](prefab_faq_bot.cpp) | Keyword-based FAQ matching |
| [prefab_concierge.cpp](prefab_concierge.cpp) | Venue information assistant |

## SWML Service Examples

| File | Description |
|------|-------------|
| [swml_service.cpp](swml_service.cpp) | Low-level SWML document builder |
| [swml_service_routing.cpp](swml_service_routing.cpp) | Multi-section routing with goto/label |
| [dynamic_swml_service.cpp](dynamic_swml_service.cpp) | Dynamic SWML per request |

## LLM Configuration

| File | Description |
|------|-------------|
| [llm_params.cpp](llm_params.cpp) | Temperature, top_p, barge_confidence tuning |
| [multi_endpoint.cpp](multi_endpoint.cpp) | Webhook URLs, query params, function includes |

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
| [rest_fabric_conferences_and_routing.cpp](../rest/examples/rest_fabric_conferences_and_routing.cpp) | Conferences, routing rules |
| [rest_phone_number_management.cpp](../rest/examples/rest_phone_number_management.cpp) | Search and manage numbers |
| [rest_video_rooms.cpp](../rest/examples/rest_video_rooms.cpp) | Video rooms, sessions, recordings |
| [rest_compat_laml.cpp](../rest/examples/rest_compat_laml.cpp) | Twilio-compatible LAML API |
| [rest_queues_mfa_and_recordings.cpp](../rest/examples/rest_queues_mfa_and_recordings.cpp) | Queues, MFA, recordings |
| [rest_10dlc_registration.cpp](../rest/examples/rest_10dlc_registration.cpp) | 10DLC registration workflow |
