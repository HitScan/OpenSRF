package org.opensrf;
import org.opensrf.net.xmpp.XMPPMessage;
import org.opensrf.util.*;
import java.util.Date;
import java.util.List;
import java.util.Iterator;


public class Stack {

    public static void processXMPPMessage(XMPPMessage msg) throws MethodException {

        if(msg == null) return;

        //System.out.println(msg.getBody());

        /** fetch this session from the cache */
        Session ses = Session.findCachedSession(msg.getThread());

        if(ses == null) {
            /** inbound client request, create a new server session */
            return;
        }

        /** parse the JSON message body, which should result in a list of OpenSRF messages */
        List msgList; 

        try {
            msgList = new JSONReader(msg.getBody()).readArray();
        } catch(JSONException e) {
            /** XXX LOG error */
            e.printStackTrace();
            return;
        }

        Iterator itr = msgList.iterator();

        OSRFObject obj = null;
        long start = new Date().getTime();

        /** cycle through the messages and push them up the stack */
        while(itr.hasNext()) {

            /** Construct a Message object from the parsed generic OSRFObject */
            obj = (OSRFObject) itr.next();

            processOSRFMessage(
                ses, 
                new Message(
                    obj.getInt("threadTrace"),
                    obj.getString("type"),
                    obj.get("payload")
                )
            );
        }

        /** LOG the duration */
    }

    private static void processOSRFMessage(Session ses, Message msg) throws MethodException {

        Logger.debug("received id=" + msg.getId() + 
            " type=" + msg.getType() + " payload=" + msg.getPayload());

        if( ses instanceof ClientSession ) 
            processResponse((ClientSession) ses, msg);
        else
            processRequest((ServerSession) ses, msg);
    }

    /** 
     * Process a server response
     */
    private static void processResponse(ClientSession session, Message msg) throws MethodException {
        String type = msg.getType();

        if(msg.RESULT.equals(type)) {
            session.pushResponse(msg);
            return;
        }

        if(msg.STATUS.equals(type)) {

            OSRFObject obj = (OSRFObject) msg.getPayload();
            Status stat = new Status(obj.getString("status"), obj.getInt("statusCode"));
            int statusCode = stat.getStatusCode();
            String status = stat.getStatus();

            switch(statusCode) {
                case Status.COMPLETE:
                    session.setRequestComplete(msg.getId());
                    break;
                case Status.NOTFOUND: 
                    session.setRequestComplete(msg.getId());
                    throw new MethodException(status);
            }
        }
    }

    /**
     * Process a client request
     */
    private static void processRequest(ServerSession session, Message msg) {
    }
}
