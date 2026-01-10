# ADR0006 Architecture

## Goal

Adopt a single-threaded event loop based on libubox/uloop to reduce complexity,
remove custom queues, and align with OpenWrt conventions.

## Core Loop

```
uloop_init();

// GPIO events
uloop_fd_add(&gpio_fd, handle_button);

// UI refresh timer (dynamic interval)
uloop_timeout_set(&ui_timer, next_refresh_ms);

// ubus async integration
ubus_add_uloop(ubus_ctx);

uloop_run();
```

## Event Sources

- GPIO: edge events from libgpiod or alternative HAL implementation.
- Timers: `uloop_timeout` drives UI refresh and animation cadence.
- ubus: async invoke + callback integrated into the same loop.

## Rendering Strategy

UI refresh runs on timer:

- Animating: 50 ms refresh
- Screen on, static: 1000 ms refresh
- Screen off: timer disabled (sleep until input)

This removes the ADR0005 push-tick chain and keeps timing local to UI state.

## HAL Retained

Keep the hardware abstraction layer to support:

- Alternative GPIO backends (libgpiod or others)
- Multiple display drivers (SSD1306, future variants)
- ubus implementation split for target vs host tests

## Differences vs ADR0005

- No event_queue/task_queue/result_queue
- No UI thread or ubus thread
- Single-threaded uloop with async ubus
- Simpler tick model via `uloop_timeout`

## Service Control Architecture

### Design Principle

Pages produce **intent**, not state mutations. State is owned by `sys_status`.

### Control Flow

```
┌─────────────────┐     ┌──────────────────┐     ┌─────────────────┐
│  page_services  │────►│  ui_controller   │────►│   sys_status    │
│                 │     │                  │     │                 │
│ - on_key()      │     │ - handle_button()│     │ - control_      │
│ - sets pending  │     │ - take_request() │     │   service()     │
│   control intent│     │ - delegates to   │     │ - ubus_hal      │
└─────────────────┘     │   sys_status     │     │   async invoke  │
        ▲               └──────────────────┘     └────────┬────────┘
        │                                                  │
        │              ┌──────────────────┐                │
        └──────────────│  control callback │◄──────────────┘
                       │  notify_result()  │
                       └──────────────────┘
```

### Key Points

- Page **never** writes `status->services[]` directly
- Control success updates `running` in `sys_status` callback (optimistic update)
- UI state (STARTING/STOPPING blink) auto-clears when actual state matches expectation
- Control failure triggers `SVC_UI_ERROR` via `notify_result()`
- Force query refresh after control to ensure eventual consistency

## Error Handling Strategy

- uloop 回调返回错误时记录日志并降级（例如保留上次渲染/状态，不阻塞主循环）。
- ubus 异步调用失败时回填失败状态，并在 UI 层显示错误态后自动恢复至上一次稳定状态。
