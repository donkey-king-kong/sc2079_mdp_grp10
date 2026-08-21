/*
package com.example.sc2079.ui.arena

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.Bundle
import android.util.Log
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import androidx.localbroadcastmanager.content.LocalBroadcastManager
import com.example.sc2079.databinding.FragmentArenaBinding
import org.json.JSONObject

class ArenaFragment : Fragment() {
    // Use activityViewModels to share the ViewModel with the hosting Activity
    private val viewModel: MainViewModel by activityViewModels()
    private val messageViewModel: MessageViewModel by activityViewModels()

    private var _binding: FragmentArenaBinding? = null
    private val binding get() = _binding!!

    val TAG = "ArenaFragment"

    // Your BroadcastReceiver can stay here
    var receiverIncomingMessages: BroadcastReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            // Your message handling logic, which is fine as is
            val incomingMessage = intent.getStringExtra("receivedMessage")
            if (incomingMessage != null){
                // ... your existing JSON parsing logic ...
            }
        }
    }

    override fun onCreateView(
        inflater: LayoutInflater, container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {
        _binding = FragmentArenaBinding.inflate(inflater, container, false)
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        // This is where you link the ViewModel to your custom View
        binding.arenaGridView.setViewModel(viewLifecycleOwner, viewModel)

        // You'll still need to handle the broadcast receiver
        LocalBroadcastManager.getInstance(requireContext())
            .registerReceiver(receiverIncomingMessages, IntentFilter("incomingMessage"))
    }

    override fun onDestroyView() {
        super.onDestroyView()
        _binding = null
        LocalBroadcastManager.getInstance(requireContext()).unregisterReceiver(receiverIncomingMessages)
    }
}
 */