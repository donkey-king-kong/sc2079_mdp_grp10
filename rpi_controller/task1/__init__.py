"""Event-driven Task 1 mission orchestration.

`runtime` owns mission state, while `workers` translate connector activity into
events without changing mission state.
"""
